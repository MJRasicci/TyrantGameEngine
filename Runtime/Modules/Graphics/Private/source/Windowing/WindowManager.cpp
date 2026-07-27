#include "Internal/Graphics/WindowManager.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Internal/Desktop/DesktopEventRuntime.hpp"
#include "Internal/Graphics/IWindowPlatform.hpp"
#include "TGE/Graphics/IWindow.hpp"

namespace TGE::Internal
{
    namespace
    {
        WindowError MakeError(WindowErrorCode code, std::string message)
        {
            return WindowError {
                .code = code,
                .message = std::move(message)
            };
        }

        WindowOperationResult ManagerStoppedResult()
        {
            return std::unexpected(MakeError(
                WindowErrorCode::ManagerStopped,
                "The window manager has stopped."));
        }

        bool IsValidBounds(const LogicalBounds& bounds) noexcept
        {
            return std::isfinite(bounds.position.x) &&
                std::isfinite(bounds.position.y) &&
                std::isfinite(bounds.size.width) &&
                std::isfinite(bounds.size.height) &&
                bounds.size.width > 0.0F &&
                bounds.size.height > 0.0F;
        }

        WindowOperationResult ExceptionResult(const char* operation) noexcept
        {
            try
            {
                throw;
            }
            catch (const std::exception& exception)
            {
                return std::unexpected(MakeError(
                    WindowErrorCode::PlatformFailure,
                    std::string(operation) + ": " + exception.what()));
            }
            catch (...)
            {
                return std::unexpected(MakeError(
                    WindowErrorCode::PlatformFailure,
                    std::string(operation) + ": unknown failure."));
            }
        }
    }

    struct WindowManager::State final
        : IWindowPlatformEventSink,
          std::enable_shared_from_this<State>
    {
        template<class TCallback>
        struct SubscriptionEntry
        {
            explicit SubscriptionEntry(TCallback callback)
                : callback(std::move(callback))
            {
            }

            std::atomic<bool> active { true };
            TCallback callback;
        };

        template<class TCallback>
        using SubscriptionMap = std::map<
            std::uint64_t,
            std::shared_ptr<SubscriptionEntry<TCallback>>>;

        struct Record
        {
            WindowId id;
            WindowDescriptor requested;

            mutable std::mutex mutex;
            WindowConfiguration configuration;
            WindowCapabilities capabilities;
            WindowLifecycleState lifecycle { WindowLifecycleState::Open };
            bool requestedInputEnabled { true };
            std::unordered_set<WindowId> suppressionSources;
            std::optional<WindowCloseReason> pendingCloseReason;
            std::uint64_t nextSubscriptionId { 1 };

            SubscriptionMap<WindowCloseRequestedCallback> closeRequested;
            SubscriptionMap<WindowClosedCallback> closed;
            SubscriptionMap<WindowMovedCallback> moved;
            SubscriptionMap<WindowResizedCallback> resized;
            SubscriptionMap<WindowScaleChangedCallback> scaleChanged;
            SubscriptionMap<WindowStateChangedCallback> stateChanged;
            SubscriptionMap<WindowFocusChangedCallback> focusChanged;
            SubscriptionMap<WindowInputChangedCallback> inputChanged;
            SubscriptionMap<WindowConfigurationChangedCallback>
                configurationChanged;
        };

        struct WindowFacade;

        struct ManagedWindow
        {
            std::shared_ptr<Record> record;
            std::shared_ptr<IWindow> facade;
        };

        static std::shared_ptr<State> Create(
            std::shared_ptr<DesktopEventRuntime> runtime,
            std::unique_ptr<IWindowPlatform> platform)
        {
            if (!runtime)
            {
                throw std::invalid_argument(
                    "A window manager requires a desktop event runtime.");
            }
            if (!platform)
            {
                throw std::invalid_argument(
                    "A window manager requires a platform implementation.");
            }

            auto result = std::shared_ptr<State>(new State);
            result->runtime = std::move(runtime);
            result->platform = std::move(platform);
            result->platform->SetEventSink(result.get());
            return result;
        }

        ~State() override = default;

        Task<WindowResult> CreateWindowAsync(WindowDescriptor descriptor)
        {
            return CreateWindowTask(
                shared_from_this(),
                std::move(descriptor));
        }

        Task<WindowOperationResult> DestroyWindowAsync(WindowId id)
        {
            return DestroyWindowTask(shared_from_this(), id);
        }

        Task<WindowOperationResult> SubmitMutation(
            std::shared_ptr<Record> record,
            std::function<WindowOperationResult(
                State&,
                const std::shared_ptr<Record>&)> mutation)
        {
            return MutationTask(
                shared_from_this(),
                std::move(record),
                std::move(mutation));
        }

        Task<void> ShutdownAsync()
        {
            return ShutdownTask(shared_from_this());
        }

        static Task<WindowResult> CreateWindowTask(
            std::shared_ptr<State> self,
            WindowDescriptor descriptor)
        {
            if (self->stopping.load())
            {
                co_return std::unexpected(MakeError(
                    WindowErrorCode::ManagerStopped,
                    "The window manager has stopped."));
            }

            try
            {
                auto runtime = self->runtime.lock();
                if (!runtime)
                {
                    co_return std::unexpected(MakeError(
                        WindowErrorCode::ManagerStopped,
                        "The desktop event runtime is unavailable."));
                }
                auto submission = runtime->Submit(
                    [self, descriptor = std::move(descriptor)]() mutable
                    {
                        return self->CreateWindowOnDispatcher(
                            std::move(descriptor));
                    });
                runtime.reset();
                co_return co_await std::move(submission);
            }
            catch (...)
            {
                const auto runtime = self->runtime.lock();
                if (self->stopping.load() ||
                    !runtime ||
                    !runtime->IsAccepting())
                {
                    co_return std::unexpected(MakeError(
                        WindowErrorCode::ManagerStopped,
                        "The window manager has stopped."));
                }
                auto failure = ExceptionResult("Creating a window failed");
                co_return std::unexpected(std::move(failure.error()));
            }
        }

        static Task<WindowOperationResult> DestroyWindowTask(
            std::shared_ptr<State> self,
            WindowId id)
        {
            if (self->stopping.load())
            {
                co_return ManagerStoppedResult();
            }

            try
            {
                auto runtime = self->runtime.lock();
                if (!runtime)
                {
                    co_return ManagerStoppedResult();
                }
                auto submission = runtime->Submit(
                    [self, id]
                    {
                        return self->DestroyWindowOnDispatcher(
                            id,
                            WindowCloseReason::ApplicationRequest,
                            false);
                    });
                runtime.reset();
                co_return co_await std::move(submission);
            }
            catch (...)
            {
                const auto runtime = self->runtime.lock();
                if (self->stopping.load() ||
                    !runtime ||
                    !runtime->IsAccepting())
                {
                    co_return ManagerStoppedResult();
                }
                co_return ExceptionResult("Destroying a window failed");
            }
        }

        static Task<WindowOperationResult> MutationTask(
            std::shared_ptr<State> self,
            std::shared_ptr<Record> record,
            std::function<WindowOperationResult(
                State&,
                const std::shared_ptr<Record>&)> mutation)
        {
            if (self->stopping.load())
            {
                co_return ManagerStoppedResult();
            }

            try
            {
                auto runtime = self->runtime.lock();
                if (!runtime)
                {
                    co_return ManagerStoppedResult();
                }
                auto submission = runtime->Submit(
                    [
                        self,
                        record = std::move(record),
                        mutation = std::move(mutation)
                    ]
                    {
                        if (self->stopping.load())
                        {
                            return ManagerStoppedResult();
                        }
                        return mutation(*self, record);
                    });
                runtime.reset();
                co_return co_await std::move(submission);
            }
            catch (...)
            {
                const auto runtime = self->runtime.lock();
                if (self->stopping.load() ||
                    !runtime ||
                    !runtime->IsAccepting())
                {
                    co_return ManagerStoppedResult();
                }
                co_return ExceptionResult("Window operation failed");
            }
        }

        static Task<void> ShutdownTask(std::shared_ptr<State> self)
        {
            if (self->stopping.exchange(true))
            {
                co_return;
            }

            bool cleanedOnRuntime = false;
            try
            {
                auto runtime = self->runtime.lock();
                if (!runtime)
                {
                    throw std::runtime_error(
                        "The desktop event runtime is unavailable.");
                }
                auto submission = runtime->Submit(
                    [self]
                    {
                        self->DestroyAllOnDispatcher();
                        self->platform->SetEventSink(nullptr);
                        self->platform->Shutdown();
                    });
                runtime.reset();
                co_await std::move(submission);
                cleanedOnRuntime = true;
            }
            catch (...)
            {
                // Detaching the sink is explicitly thread-safe. Thread-affine
                // cleanup cannot be recovered after the shared runtime stops,
                // but public facades still transition to a terminal state.
                self->platform->SetEventSink(nullptr);
            }

            if (cleanedOnRuntime)
            {
                self->MarkAllDestroyed(
                    WindowCloseReason::ApplicationRequest);
            }
            else
            {
                self->AbandonAllWindows();
            }
        }

        std::shared_ptr<IWindow> FindWindow(WindowId id) const noexcept
        {
            std::scoped_lock lock(mutex);
            const auto found = windows.find(id);
            if (found == windows.end())
            {
                return {};
            }
            return found->second.facade;
        }

        std::vector<std::shared_ptr<IWindow>> Windows() const
        {
            std::vector<std::shared_ptr<IWindow>> result;
            std::scoped_lock lock(mutex);
            result.reserve(order.size());
            for (const auto id : order)
            {
                const auto found = windows.find(id);
                if (found != windows.end())
                {
                    result.emplace_back(found->second.facade);
                }
            }
            return result;
        }

        void Shutdown() noexcept
        {
            if (stopping.load())
            {
                return;
            }

            const auto currentRuntime = runtime.lock();
            if (!currentRuntime ||
                !currentRuntime->IsEventThread())
            {
                ShutdownFallback();
                return;
            }

            try
            {
                auto completion =
                    Execution::SyncWait(ShutdownAsync());
                (void)completion;
            }
            catch (...)
            {
                platform->SetEventSink(nullptr);
                MarkAllDestroyed(
                    WindowCloseReason::ApplicationRequest);
            }
        }

        void ShutdownFallback() noexcept
        {
            if (stopping.exchange(true))
            {
                return;
            }

            try
            {
                // Sink detachment is the SPI's cross-thread quiescence
                // boundary. Public facades become terminal immediately while
                // thread-affine platform cleanup is queued without blocking a
                // runtime that may not have started yet.
                platform->SetEventSink(nullptr);
                std::vector<WindowId> deferredWindows;
                {
                    std::scoped_lock lock(mutex);
                    deferredWindows.assign(
                        order.rbegin(),
                        order.rend());
                }
                AbandonAllWindows();

                auto deferredPlatform = platform;
                if (const auto currentRuntime = runtime.lock())
                {
                    (void)currentRuntime->Post(
                        [
                            deferredPlatform =
                                std::move(deferredPlatform),
                            deferredWindows =
                                std::move(deferredWindows)
                        ]() noexcept
                        {
                            for (const auto id : deferredWindows)
                            {
                                try
                                {
                                    (void)deferredPlatform->DestroyWindow(id);
                                }
                                catch (...)
                                {
                                }
                            }
                            deferredPlatform->Shutdown();
                        });
                }
            }
            catch (...)
            {
                try
                {
                    platform->SetEventSink(nullptr);
                }
                catch (...)
                {
                }
                AbandonAllWindows();
            }
        }

        bool OnPlatformCloseRequested(
            WindowId id,
            WindowCloseReason reason) noexcept override
        {
            try
            {
                const auto record = FindRecord(id);
                if (!record)
                {
                    return false;
                }
                FlushPendingConfiguration(id);
                return DispatchCloseRequested(record, reason);
            }
            catch (...)
            {
                return false;
            }
        }

        void OnPlatformConfigurationChanged(
            WindowId id,
            WindowConfiguration configuration) noexcept override
        {
            try
            {
                const auto [found, inserted] =
                    pendingConfigurations.insert_or_assign(
                        id,
                        std::move(configuration));
                (void)found;
                if (!inserted)
                {
                    return;
                }

                auto self = shared_from_this();
                const auto currentRuntime = runtime.lock();
                if (!currentRuntime ||
                    !currentRuntime->Post(
                        [self, id]
                        {
                            self->FlushPendingConfiguration(id);
                        }))
                {
                    FlushPendingConfiguration(id);
                }
            }
            catch (...)
            {
                // Backends report events through a noexcept boundary. A bad
                // application callback must not terminate the platform pump.
            }
        }

        void OnPlatformClosed(
            WindowId id,
            WindowCloseReason reason) noexcept override
        {
            try
            {
                FlushPendingConfiguration(id);
                const auto record = FindRecord(id);
                if (record)
                {
                    FinalizeClosed(record, reason);
                }
            }
            catch (...)
            {
                // See OnPlatformConfigurationChanged.
            }
        }

        void FlushPendingConfiguration(WindowId id)
        {
            const auto pending = pendingConfigurations.find(id);
            if (pending == pendingConfigurations.end())
            {
                return;
            }

            auto configuration = std::move(pending->second);
            pendingConfigurations.erase(pending);

            const auto record = FindRecord(id);
            if (!record)
            {
                return;
            }

            bool suppressed = false;
            {
                std::scoped_lock lock(record->mutex);
                suppressed = !record->suppressionSources.empty();
            }

            if (suppressed && configuration.inputEnabled)
            {
                auto correction =
                    platform->SetInputEnabled(id, false);
                if (correction)
                {
                    configuration =
                        std::move(correction->configuration);
                }
                configuration.inputEnabled = false;
            }

            ApplyConfiguration(record, std::move(configuration));
        }

        WindowResult CreateWindowOnDispatcher(WindowDescriptor descriptor);

        WindowOperationResult DestroyWindowOnDispatcher(
            WindowId id,
            WindowCloseReason reason,
            bool forceTerminal)
        {
            const auto record = FindRecord(id);
            if (!record)
            {
                return std::unexpected(MakeError(
                    WindowErrorCode::WindowDestroyed,
                    "The window is no longer managed."));
            }

            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle == WindowLifecycleState::Destroyed)
                {
                    return std::unexpected(MakeError(
                        WindowErrorCode::WindowDestroyed,
                        "The window has already been destroyed."));
                }
                if (record->lifecycle == WindowLifecycleState::Destroying)
                {
                    return std::unexpected(MakeError(
                        WindowErrorCode::InvalidState,
                        "The window is already being destroyed."));
                }
                record->lifecycle = WindowLifecycleState::Destroying;
                record->pendingCloseReason = reason;
            }

            auto result = platform->DestroyWindow(id);
            if (!result)
            {
                if (forceTerminal)
                {
                    FinalizeClosed(record, reason);
                }
                else
                {
                    std::scoped_lock lock(record->mutex);
                    if (record->lifecycle !=
                        WindowLifecycleState::Destroyed)
                    {
                        record->lifecycle = WindowLifecycleState::Open;
                        record->pendingCloseReason.reset();
                    }
                }
                return result;
            }

            // Backends may report closure synchronously or after returning.
            // An unconditional explicit destroy is terminal once it succeeds.
            if (Lifecycle(record) != WindowLifecycleState::Destroyed)
            {
                FinalizeClosed(record, reason);
            }
            return result;
        }

        WindowOperationResult MutateConfiguration(
            const std::shared_ptr<Record>& record,
            std::function<WindowPlatformMutationResult(IWindowPlatform&)>
                operation)
        {
            if (!IsOpen(record))
            {
                return std::unexpected(MakeError(
                    WindowErrorCode::WindowDestroyed,
                    "The window is not open."));
            }

            auto result = operation(*platform);
            if (!result)
            {
                return std::unexpected(std::move(result.error()));
            }

            ApplyConfiguration(
                record,
                std::move(result->configuration));
            return result->status;
        }

        WindowOperationResult SetInputEnabledOnDispatcher(
            const std::shared_ptr<Record>& record,
            bool enabled)
        {
            bool effective = false;
            bool currentlyEffective = false;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle != WindowLifecycleState::Open)
                {
                    return std::unexpected(MakeError(
                        WindowErrorCode::WindowDestroyed,
                        "The window is not open."));
                }
                record->requestedInputEnabled = enabled;
                effective =
                    enabled && record->suppressionSources.empty();
                currentlyEffective =
                    record->configuration.inputEnabled;
            }

            if (effective == currentlyEffective)
            {
                return effective == enabled
                    ? WindowOperationStatus::Applied
                    : WindowOperationStatus::Normalized;
            }

            auto result =
                platform->SetInputEnabled(record->id, effective);
            if (!result)
            {
                return std::unexpected(std::move(result.error()));
            }

            auto configuration = std::move(result->configuration);
            configuration.inputEnabled = effective;
            ApplyConfiguration(record, std::move(configuration));

            return effective == enabled
                ? result->status
                : WindowOperationStatus::Normalized;
        }

        WindowOperationResult RequestCloseOnDispatcher(
            const std::shared_ptr<Record>& record)
        {
            if (!IsOpen(record))
            {
                return std::unexpected(MakeError(
                    WindowErrorCode::WindowDestroyed,
                    "The window is not open."));
            }

            {
                std::scoped_lock lock(record->mutex);
                record->pendingCloseReason =
                    WindowCloseReason::ApplicationRequest;
            }

            auto result =
                platform->RequestClose(record->id);
            if (!result)
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle == WindowLifecycleState::Open)
                {
                    record->pendingCloseReason.reset();
                }
            }
            else if (*result == WindowOperationStatus::Cancelled)
            {
                std::scoped_lock lock(record->mutex);
                record->pendingCloseReason.reset();
            }
            return result;
        }

        void ApplyConfiguration(
            const std::shared_ptr<Record>& record,
            WindowConfiguration configuration)
        {
            WindowConfiguration previous;
            bool movedChanged = false;
            bool resizedChanged = false;
            bool scaleChanged = false;
            bool stateChanged = false;
            bool focusChanged = false;
            bool inputChanged = false;
            bool suppressionPolicyChanged = false;

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowMovedCallback>>> movedCallbacks;
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowResizedCallback>>>
                resizedCallbacks;
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowScaleChangedCallback>>>
                scaleCallbacks;
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowStateChangedCallback>>>
                stateCallbacks;
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowFocusChangedCallback>>>
                focusCallbacks;
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowInputChangedCallback>>>
                inputCallbacks;
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<
                        WindowConfigurationChangedCallback>>>
                configurationCallbacks;

            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle ==
                    WindowLifecycleState::Destroyed)
                {
                    return;
                }

                if (!record->suppressionSources.empty())
                {
                    configuration.inputEnabled = false;
                }

                previous = record->configuration;
                if (previous == configuration)
                {
                    return;
                }

                movedChanged =
                    previous.geometry.logicalBounds.position !=
                    configuration.geometry.logicalBounds.position;
                resizedChanged =
                    previous.geometry.logicalBounds.size !=
                        configuration.geometry.logicalBounds.size ||
                    previous.geometry.framebufferSize !=
                        configuration.geometry.framebufferSize;
                scaleChanged =
                    previous.geometry.scale != configuration.geometry.scale;
                stateChanged = previous.state != configuration.state;
                focusChanged = previous.focused != configuration.focused;
                inputChanged =
                    previous.inputEnabled != configuration.inputEnabled;
                suppressionPolicyChanged =
                    previous.parent != configuration.parent ||
                    previous.modality != configuration.modality;

                record->configuration = configuration;
                movedCallbacks = Snapshot(record->moved);
                resizedCallbacks = Snapshot(record->resized);
                scaleCallbacks = Snapshot(record->scaleChanged);
                stateCallbacks = Snapshot(record->stateChanged);
                focusCallbacks = Snapshot(record->focusChanged);
                inputCallbacks = Snapshot(record->inputChanged);
                configurationCallbacks =
                    Snapshot(record->configurationChanged);
            }

            // Property events precede the aggregate configuration event.
            // Scale precedes resize so renderers can update scale-dependent
            // layout before consuming the new framebuffer dimensions.
            if (movedChanged)
            {
                Invoke(
                    movedCallbacks,
                    WindowMovedEvent {
                        .window = record->id,
                        .previous =
                            previous.geometry.logicalBounds.position,
                        .current =
                            configuration.geometry.logicalBounds.position
                    });
            }
            if (scaleChanged)
            {
                Invoke(
                    scaleCallbacks,
                    WindowScaleChangedEvent {
                        .window = record->id,
                        .previous = previous.geometry,
                        .current = configuration.geometry
                    });
            }
            if (resizedChanged)
            {
                Invoke(
                    resizedCallbacks,
                    WindowResizedEvent {
                        .window = record->id,
                        .previous = previous.geometry,
                        .current = configuration.geometry
                    });
            }
            if (stateChanged)
            {
                Invoke(
                    stateCallbacks,
                    WindowStateChangedEvent {
                        .window = record->id,
                        .previous = previous.state,
                        .current = configuration.state
                    });
            }
            if (focusChanged)
            {
                Invoke(
                    focusCallbacks,
                    WindowFocusChangedEvent {
                        .window = record->id,
                        .focused = configuration.focused
                    });
            }
            if (inputChanged)
            {
                Invoke(
                    inputCallbacks,
                    WindowInputChangedEvent {
                        .window = record->id,
                        .enabled = configuration.inputEnabled
                    });
            }

            Invoke(
                configurationCallbacks,
                WindowConfigurationChangedEvent {
                    .window = record->id,
                    .previous = std::move(previous),
                    .current = std::move(configuration)
                });

            // A backend can normalize an ownership relationship after
            // creation, most notably when a retained child loses its parent.
            // Rebuild the complete suppression graph because changing one
            // ancestor can alter a nested modal's entire parent tree.
            if (suppressionPolicyChanged)
            {
                ReconcileModalSuppression();
            }
        }

        bool DispatchCloseRequested(
            const std::shared_ptr<Record>& record,
            WindowCloseReason reason)
        {
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowCloseRequestedCallback>>>
                callbacks;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle != WindowLifecycleState::Open)
                {
                    return false;
                }
                record->pendingCloseReason = reason;
                callbacks = Snapshot(record->closeRequested);
            }

            WindowCloseRequestedEvent event(record->id, reason);
            for (const auto& callback : callbacks)
            {
                if (!callback->active.load())
                {
                    continue;
                }
                try
                {
                    callback->callback(event);
                }
                catch (...)
                {
                    // One subscriber cannot prevent later policy subscribers.
                }
            }

            if (event.IsCancelled())
            {
                std::scoped_lock lock(record->mutex);
                record->pendingCloseReason.reset();
                return false;
            }
            return true;
        }

        void FinalizeClosed(
            const std::shared_ptr<Record>& record,
            WindowCloseReason reportedReason)
        {
            WindowCloseReason reason = reportedReason;
            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<WindowClosedCallback>>> callbacks;

            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle ==
                    WindowLifecycleState::Destroyed)
                {
                    return;
                }
                if (record->pendingCloseReason)
                {
                    reason = *record->pendingCloseReason;
                }
                record->pendingCloseReason.reset();
                record->lifecycle = WindowLifecycleState::Destroyed;
                callbacks = Snapshot(record->closed);
            }

            ReleaseModalSuppression(record);

            {
                std::scoped_lock lock(mutex);
                windows.erase(record->id);
                std::erase(order, record->id);
            }

            Invoke(
                callbacks,
                WindowClosedEvent {
                    .window = record->id,
                    .reason = reason
                });
        }

        void RefreshModalSuppressionForNewWindow(
            const std::shared_ptr<Record>& created)
        {
            std::vector<std::shared_ptr<Record>> records;
            {
                std::scoped_lock lock(mutex);
                records.reserve(order.size());
                for (const auto id : order)
                {
                    const auto found = windows.find(id);
                    if (found != windows.end())
                    {
                        records.emplace_back(found->second.record);
                    }
                }
            }

            WindowModality createdModality;
            {
                std::scoped_lock lock(created->mutex);
                createdModality = created->configuration.modality;
            }

            // A newly created modal becomes the active modal layer. Older
            // application-modal windows remain suppressed by it and resume
            // their policy when the newer modal closes.
            if (createdModality == WindowModality::Modeless)
            {
                for (const auto& modal : records)
                {
                    if (modal == created)
                    {
                        continue;
                    }
                    if (ShouldSuppress(modal, created))
                    {
                        AddSuppression(created, modal->id);
                    }
                }
            }

            for (const auto& target : records)
            {
                if (target == created)
                {
                    continue;
                }
                if (ShouldSuppress(created, target))
                {
                    AddSuppression(target, created->id);
                }
            }
        }

        bool ShouldSuppress(
            const std::shared_ptr<Record>& modal,
            const std::shared_ptr<Record>& target)
        {
            WindowModality modality;
            std::optional<WindowId> parent;
            {
                std::scoped_lock lock(modal->mutex);
                if (modal->lifecycle != WindowLifecycleState::Open)
                {
                    return false;
                }
                modality = modal->configuration.modality;
                parent = modal->configuration.parent;
            }

            switch (modality)
            {
            case WindowModality::Modeless:
                return false;
            case WindowModality::ApplicationModal:
                return modal->id != target->id;
            case WindowModality::DisableParent:
                return parent && *parent == target->id;
            case WindowModality::DisableParentTree:
                break;
            }

            while (parent)
            {
                if (*parent == target->id)
                {
                    return true;
                }
                const auto ancestor = FindRecord(*parent);
                if (!ancestor)
                {
                    break;
                }
                std::scoped_lock lock(ancestor->mutex);
                parent = ancestor->configuration.parent;
            }
            return false;
        }

        void ReconcileModalSuppression()
        {
            std::vector<std::shared_ptr<Record>> records;
            {
                std::scoped_lock lock(mutex);
                records.reserve(order.size());
                for (const auto id : order)
                {
                    const auto found = windows.find(id);
                    if (found != windows.end() &&
                        IsOpen(found->second.record))
                    {
                        records.emplace_back(found->second.record);
                    }
                }
            }

            std::unordered_map<
                WindowId,
                std::unordered_set<WindowId>> desiredSources;
            desiredSources.reserve(records.size());
            for (const auto& record : records)
            {
                desiredSources.try_emplace(record->id);
            }

            // Replay creation order to retain the modal-layer rule used when
            // windows are first opened: a newly created modal supersedes older
            // modal layers, while a later modeless window is suppressed by
            // every earlier modal whose effective policy targets it.
            for (std::size_t index = 0; index < records.size(); ++index)
            {
                const auto& current = records[index];
                const auto currentConfiguration = Configuration(current);

                if (currentConfiguration.modality ==
                    WindowModality::Modeless)
                {
                    for (std::size_t prior = 0; prior < index; ++prior)
                    {
                        if (ShouldSuppress(records[prior], current))
                        {
                            desiredSources[current->id].emplace(
                                records[prior]->id);
                        }
                    }
                }

                for (std::size_t prior = 0; prior < index; ++prior)
                {
                    if (ShouldSuppress(current, records[prior]))
                    {
                        desiredSources[records[prior]->id].emplace(
                            current->id);
                    }
                }
            }

            for (const auto& target : records)
            {
                bool updatePlatform = false;
                bool effectiveInput = false;
                {
                    std::scoped_lock lock(target->mutex);
                    if (target->lifecycle !=
                        WindowLifecycleState::Open)
                    {
                        continue;
                    }

                    target->suppressionSources =
                        std::move(desiredSources[target->id]);
                    effectiveInput =
                        target->requestedInputEnabled &&
                        target->suppressionSources.empty();
                    updatePlatform =
                        effectiveInput !=
                        target->configuration.inputEnabled;
                }

                if (!updatePlatform)
                {
                    continue;
                }

                auto result = platform->SetInputEnabled(
                    target->id,
                    effectiveInput);
                if (result)
                {
                    auto configuration =
                        std::move(result->configuration);
                    configuration.inputEnabled = effectiveInput;
                    ApplyConfiguration(
                        target,
                        std::move(configuration));
                }
                else if (!effectiveInput)
                {
                    // Suppression remains authoritative even if a backend
                    // cannot express it through a native window attribute.
                    auto configuration = Configuration(target);
                    configuration.inputEnabled = false;
                    ApplyConfiguration(
                        target,
                        std::move(configuration));
                }
            }
        }

        void AddSuppression(
            const std::shared_ptr<Record>& target,
            WindowId source)
        {
            bool needsPlatformUpdate = false;
            {
                std::scoped_lock lock(target->mutex);
                if (target->lifecycle != WindowLifecycleState::Open ||
                    !target->suppressionSources.emplace(source).second)
                {
                    return;
                }
                needsPlatformUpdate =
                    target->configuration.inputEnabled;
            }

            if (!needsPlatformUpdate)
            {
                return;
            }

            auto result =
                platform->SetInputEnabled(target->id, false);
            if (result)
            {
                auto configuration = std::move(result->configuration);
                configuration.inputEnabled = false;
                ApplyConfiguration(target, std::move(configuration));
            }
            else
            {
                auto configuration = Configuration(target);
                configuration.inputEnabled = false;
                ApplyConfiguration(target, std::move(configuration));
            }
        }

        void ReleaseModalSuppression(
            const std::shared_ptr<Record>& modal)
        {
            std::vector<std::shared_ptr<Record>> records;
            {
                std::scoped_lock lock(mutex);
                records.reserve(windows.size());
                for (const auto& [id, managed] : windows)
                {
                    (void)id;
                    records.emplace_back(managed.record);
                }
            }

            for (const auto& target : records)
            {
                if (target == modal)
                {
                    continue;
                }

                bool restore = false;
                {
                    std::scoped_lock lock(target->mutex);
                    if (target->lifecycle ==
                            WindowLifecycleState::Destroyed ||
                        target->suppressionSources.erase(modal->id) == 0)
                    {
                        continue;
                    }
                    restore =
                        target->suppressionSources.empty() &&
                        target->requestedInputEnabled &&
                        !target->configuration.inputEnabled;
                }

                if (!restore)
                {
                    continue;
                }

                auto result = platform->SetInputEnabled(
                    target->id,
                    true);
                if (result)
                {
                    auto configuration = std::move(result->configuration);
                    configuration.inputEnabled = true;
                    ApplyConfiguration(target, std::move(configuration));
                }
            }
        }

        void DestroyAllOnDispatcher() noexcept
        {
            std::vector<WindowId> ids;
            {
                std::scoped_lock lock(mutex);
                ids.assign(order.rbegin(), order.rend());
            }

            for (const auto id : ids)
            {
                try
                {
                    (void)DestroyWindowOnDispatcher(
                        id,
                        WindowCloseReason::ApplicationRequest,
                        true);
                }
                catch (...)
                {
                    const auto record = FindRecord(id);
                    if (record)
                    {
                        FinalizeClosed(
                            record,
                            WindowCloseReason::ApplicationRequest);
                    }
                }
            }
        }

        void MarkAllDestroyed(WindowCloseReason reason) noexcept
        {
            std::vector<std::shared_ptr<Record>> records;
            {
                std::scoped_lock lock(mutex);
                records.reserve(windows.size());
                for (const auto& [id, managed] : windows)
                {
                    (void)id;
                    records.emplace_back(managed.record);
                }
            }

            for (const auto& record : records)
            {
                try
                {
                    FinalizeClosed(record, reason);
                }
                catch (...)
                {
                    std::scoped_lock lock(record->mutex);
                    record->lifecycle =
                        WindowLifecycleState::Destroyed;
                }
            }

            std::scoped_lock lock(mutex);
            windows.clear();
            order.clear();
        }

        template<class TCallback>
        static void DeactivateAndClear(
            SubscriptionMap<TCallback>& subscriptions) noexcept
        {
            for (auto& [id, subscription] : subscriptions)
            {
                (void)id;
                subscription->active.store(false);
            }
            subscriptions.clear();
        }

        void AbandonAllWindows() noexcept
        {
            std::vector<std::shared_ptr<Record>> records;
            {
                std::scoped_lock lock(mutex);
                records.reserve(windows.size());
                for (const auto& [id, managed] : windows)
                {
                    (void)id;
                    records.emplace_back(managed.record);
                }
                windows.clear();
                order.clear();
                pendingConfigurations.clear();
            }

            for (const auto& record : records)
            {
                std::scoped_lock lock(record->mutex);
                record->pendingCloseReason.reset();
                record->suppressionSources.clear();
                record->lifecycle =
                    WindowLifecycleState::Destroyed;
                DeactivateAndClear(record->closeRequested);
                DeactivateAndClear(record->closed);
                DeactivateAndClear(record->moved);
                DeactivateAndClear(record->resized);
                DeactivateAndClear(record->scaleChanged);
                DeactivateAndClear(record->stateChanged);
                DeactivateAndClear(record->focusChanged);
                DeactivateAndClear(record->inputChanged);
                DeactivateAndClear(record->configurationChanged);
            }
        }

        std::shared_ptr<Record> FindRecord(WindowId id) const noexcept
        {
            std::scoped_lock lock(mutex);
            const auto found = windows.find(id);
            return found == windows.end()
                ? std::shared_ptr<Record> {}
                : found->second.record;
        }

        static bool IsOpen(const std::shared_ptr<Record>& record) noexcept
        {
            return Lifecycle(record) == WindowLifecycleState::Open;
        }

        static WindowLifecycleState Lifecycle(
            const std::shared_ptr<Record>& record) noexcept
        {
            std::scoped_lock lock(record->mutex);
            return record->lifecycle;
        }

        static WindowConfiguration Configuration(
            const std::shared_ptr<Record>& record)
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration;
        }

        template<class TCallback>
        static std::vector<std::shared_ptr<SubscriptionEntry<TCallback>>>
            Snapshot(const SubscriptionMap<TCallback>& subscriptions)
        {
            std::vector<std::shared_ptr<SubscriptionEntry<TCallback>>>
                result;
            result.reserve(subscriptions.size());
            for (const auto& [id, subscription] : subscriptions)
            {
                (void)id;
                result.emplace_back(subscription);
            }
            return result;
        }

        template<class TCallback, class TEvent>
        static void Invoke(
            const std::vector<
                std::shared_ptr<SubscriptionEntry<TCallback>>>& callbacks,
            const TEvent& event) noexcept
        {
            for (const auto& callback : callbacks)
            {
                if (!callback->active.load())
                {
                    continue;
                }
                try
                {
                    callback->callback(event);
                }
                catch (...)
                {
                    // Event delivery is isolated per subscriber.
                }
            }
        }

        template<class TCallback>
        static WindowSubscription Subscribe(
            const std::shared_ptr<Record>& record,
            SubscriptionMap<TCallback> Record::*collection,
            TCallback callback)
        {
            if (!callback)
            {
                return {};
            }

            auto entry = std::make_shared<SubscriptionEntry<TCallback>>(
                std::move(callback));
            std::uint64_t id = 0;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle ==
                    WindowLifecycleState::Destroyed)
                {
                    return {};
                }
                id = record->nextSubscriptionId++;
                (record.get()->*collection).emplace(id, entry);
            }

            std::weak_ptr<Record> weakRecord = record;
            return WindowSubscription(
                [
                    weakRecord,
                    collection,
                    id,
                    entry = std::move(entry)
                ]() noexcept
                {
                    entry->active.store(false);
                    const auto record = weakRecord.lock();
                    if (!record)
                    {
                        return;
                    }
                    std::scoped_lock lock(record->mutex);
                    (record.get()->*collection).erase(id);
                });
        }

        mutable std::mutex mutex;
        std::unordered_map<WindowId, ManagedWindow> windows;
        std::vector<WindowId> order;
        std::unordered_map<WindowId, WindowConfiguration>
            pendingConfigurations;
        std::atomic<std::uint64_t> nextWindowId { 1 };
        std::atomic<bool> stopping { false };
        std::weak_ptr<DesktopEventRuntime> runtime;
        std::shared_ptr<IWindowPlatform> platform;
    };

    struct WindowManager::State::WindowFacade final : IWindow
    {
        WindowFacade(
            std::weak_ptr<WindowManager::State> manager,
            std::shared_ptr<Record> record)
            : manager(std::move(manager)),
              record(std::move(record))
        {
        }

        WindowId Id() const noexcept override
        {
            return record->id;
        }

        WindowDescriptor RequestedDescriptor() const override
        {
            std::scoped_lock lock(record->mutex);
            return record->requested;
        }

        WindowConfiguration EffectiveConfiguration() const override
        {
            return Configuration(record);
        }

        WindowCapabilities Capabilities() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->capabilities;
        }

        WindowLifecycleState LifecycleState() const noexcept override
        {
            return Lifecycle(record);
        }

        std::string Title() const override
        {
            return EffectiveConfiguration().title;
        }

        WindowRole Role() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration.role;
        }

        std::optional<WindowId> ParentId() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration.parent;
        }

        WindowGeometry Geometry() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration.geometry;
        }

        WindowState State() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration.state;
        }

        bool IsVisible() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration.visible;
        }

        bool IsFocused() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration.focused;
        }

        bool IsInputEnabled() const noexcept override
        {
            std::scoped_lock lock(record->mutex);
            return record->configuration.inputEnabled;
        }

        Task<WindowOperationResult> SetTitleAsync(
            std::string title) override
        {
            return Submit(
                [title = std::move(title)](
                    WindowManager::State& state,
                    const std::shared_ptr<Record>& record) mutable
                {
                    return state.MutateConfiguration(
                        record,
                        [
                            id = record->id,
                            title = std::move(title)
                        ](IWindowPlatform& platform) mutable
                        {
                            return platform.SetTitle(
                                id,
                                std::move(title));
                        });
                });
        }

        Task<WindowOperationResult> SetLogicalBoundsAsync(
            LogicalBounds bounds) override
        {
            if (!IsValidBounds(bounds))
            {
                return Completed(std::unexpected(MakeError(
                    WindowErrorCode::InvalidDescriptor,
                    "Window bounds must be finite and have a positive size.")));
            }
            return Submit(
                [bounds](
                    WindowManager::State& state,
                    const std::shared_ptr<Record>& record)
                {
                    return state.MutateConfiguration(
                        record,
                        [id = record->id, bounds](
                            IWindowPlatform& platform)
                        {
                            return platform.SetLogicalBounds(id, bounds);
                        });
                });
        }

        Task<WindowOperationResult> SetStateAsync(
            WindowState windowState) override
        {
            return Submit(
                [windowState](
                    WindowManager::State& state,
                    const std::shared_ptr<Record>& record)
                {
                    return state.MutateConfiguration(
                        record,
                        [id = record->id, windowState](
                            IWindowPlatform& platform)
                        {
                            return platform.SetState(id, windowState);
                        });
                });
        }

        Task<WindowOperationResult> ShowAsync() override
        {
            return SetVisibleAsync(true);
        }

        Task<WindowOperationResult> HideAsync() override
        {
            return SetVisibleAsync(false);
        }

        Task<WindowOperationResult> RequestFocusAsync() override
        {
            return Submit(
                [](
                    WindowManager::State& state,
                    const std::shared_ptr<Record>& record)
                {
                    return state.MutateConfiguration(
                        record,
                        [id = record->id](IWindowPlatform& platform)
                        {
                            return platform.RequestFocus(id);
                        });
                });
        }

        Task<WindowOperationResult> SetInputEnabledAsync(
            bool enabled) override
        {
            return Submit(
                [enabled](
                    WindowManager::State& state,
                    const std::shared_ptr<Record>& record)
                {
                    return state.SetInputEnabledOnDispatcher(
                        record,
                        enabled);
                });
        }

        Task<WindowOperationResult> RequestCloseAsync() override
        {
            return Submit(
                [](
                    WindowManager::State& state,
                    const std::shared_ptr<Record>& record)
                {
                    return state.RequestCloseOnDispatcher(record);
                });
        }

        WindowSubscription SubscribeCloseRequested(
            WindowCloseRequestedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::closeRequested,
                std::move(callback));
        }

        WindowSubscription SubscribeClosed(
            WindowClosedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::closed,
                std::move(callback));
        }

        WindowSubscription SubscribeMoved(
            WindowMovedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::moved,
                std::move(callback));
        }

        WindowSubscription SubscribeResized(
            WindowResizedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::resized,
                std::move(callback));
        }

        WindowSubscription SubscribeScaleChanged(
            WindowScaleChangedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::scaleChanged,
                std::move(callback));
        }

        WindowSubscription SubscribeStateChanged(
            WindowStateChangedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::stateChanged,
                std::move(callback));
        }

        WindowSubscription SubscribeFocusChanged(
            WindowFocusChangedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::focusChanged,
                std::move(callback));
        }

        WindowSubscription SubscribeInputChanged(
            WindowInputChangedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::inputChanged,
                std::move(callback));
        }

        WindowSubscription SubscribeConfigurationChanged(
            WindowConfigurationChangedCallback callback) override
        {
            return Subscribe(
                record,
                &Record::configurationChanged,
                std::move(callback));
        }

    private:
        Task<WindowOperationResult> SetVisibleAsync(bool visible)
        {
            return Submit(
                [visible](
                    WindowManager::State& state,
                    const std::shared_ptr<Record>& record)
                {
                    return state.MutateConfiguration(
                        record,
                        [id = record->id, visible](
                            IWindowPlatform& platform)
                        {
                            return platform.SetVisible(id, visible);
                        });
                });
        }

        Task<WindowOperationResult> Submit(
            std::function<WindowOperationResult(
                WindowManager::State&,
                const std::shared_ptr<Record>&)> operation)
        {
            const auto state = manager.lock();
            if (!state)
            {
                return Completed(ManagerStoppedResult());
            }
            return state->SubmitMutation(
                record,
                std::move(operation));
        }

        static Task<WindowOperationResult> Completed(
            WindowOperationResult result)
        {
            co_return result;
        }

        std::weak_ptr<WindowManager::State> manager;
        std::shared_ptr<Record> record;
    };

    WindowResult WindowManager::State::CreateWindowOnDispatcher(
        WindowDescriptor descriptor)
    {
        if (stopping.load())
        {
            return std::unexpected(MakeError(
                WindowErrorCode::ManagerStopped,
                "The window manager has stopped."));
        }

        if (!IsValidBounds(descriptor.bounds))
        {
            return std::unexpected(MakeError(
                WindowErrorCode::InvalidDescriptor,
                "Window bounds must be finite and have a positive size."));
        }

        if ((descriptor.role == WindowRole::Child ||
             descriptor.modality == WindowModality::DisableParent ||
             descriptor.modality == WindowModality::DisableParentTree) &&
            !descriptor.parent)
        {
            return std::unexpected(MakeError(
                WindowErrorCode::InvalidDescriptor,
                "This window relationship requires a parent."));
        }

        if (descriptor.parent)
        {
            const auto parent = FindRecord(*descriptor.parent);
            if (!parent || !IsOpen(parent))
            {
                return std::unexpected(MakeError(
                    WindowErrorCode::ParentNotFound,
                    "The requested parent window is not open."));
            }
        }

        const auto id =
            WindowId::FromValue(nextWindowId.fetch_add(1));
        auto platformResult =
            platform->CreateWindow(id, descriptor);
        if (!platformResult)
        {
            return std::unexpected(std::move(platformResult.error()));
        }

        auto record = std::make_shared<Record>();
        record->id = id;
        record->requested = descriptor;
        record->configuration =
            std::move(platformResult->configuration);
        record->capabilities = platformResult->capabilities;
        record->requestedInputEnabled = descriptor.acceptsInput;

        // Application modality can be implemented by the facade whenever the
        // backend supports explicit input control.
        record->capabilities.applicationModality =
            record->capabilities.applicationModality ||
            record->capabilities.inputControl;
        if (descriptor.modality != WindowModality::Modeless &&
            (record->capabilities.inputControl ||
             (descriptor.modality ==
                  WindowModality::ApplicationModal &&
              record->capabilities.applicationModality)))
        {
            record->configuration.modality =
                descriptor.modality;
        }

        auto facade = std::make_shared<WindowFacade>(
            weak_from_this(),
            record);

        {
            std::scoped_lock lock(mutex);
            windows.emplace(
                id,
                ManagedWindow {
                    .record = record,
                    .facade = facade
                });
            order.emplace_back(id);
        }

        RefreshModalSuppressionForNewWindow(record);
        return std::static_pointer_cast<IWindow>(std::move(facade));
    }

    WindowManager::WindowManager(
        std::shared_ptr<DesktopEventRuntime> runtime,
        std::unique_ptr<IWindowPlatform> platform)
        : state(State::Create(
              std::move(runtime),
              std::move(platform)))
    {
    }

    WindowManager::~WindowManager()
    {
        if (state)
        {
            state->Shutdown();
        }
    }

    Task<WindowResult> WindowManager::CreateWindowAsync(
        WindowDescriptor descriptor)
    {
        return state->CreateWindowAsync(std::move(descriptor));
    }

    std::shared_ptr<IWindow> WindowManager::FindWindow(
        WindowId id) const noexcept
    {
        return state->FindWindow(id);
    }

    std::vector<std::shared_ptr<IWindow>> WindowManager::Windows() const
    {
        return state->Windows();
    }

    Task<WindowOperationResult> WindowManager::DestroyWindowAsync(
        WindowId id)
    {
        return state->DestroyWindowAsync(id);
    }

    Task<void> WindowManager::ShutdownAsync()
    {
        return state->ShutdownAsync();
    }
}
