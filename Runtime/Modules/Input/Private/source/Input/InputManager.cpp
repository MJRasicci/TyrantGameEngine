/**
 * @file InputManager.cpp
 * @brief Implements serialized input contexts over DesktopEventRuntime.
 */

#include "Internal/Input/InputManager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "Internal/Desktop/DesktopEventRuntime.hpp"
#include "Internal/Input/IInputPlatform.hpp"

namespace
{
    TGE::InputError MakeInputError(
        TGE::InputErrorCode code,
        std::string message)
    {
        return TGE::InputError {
            .code = code,
            .message = std::move(message)
        };
    }

    TGE::InputError CurrentExceptionError(
        const char* operation) noexcept
    {
        try
        {
            throw;
        }
        catch (const std::exception& exception)
        {
            return MakeInputError(
                TGE::InputErrorCode::PlatformFailure,
                std::string(operation) + ": " + exception.what());
        }
        catch (...)
        {
            return MakeInputError(
                TGE::InputErrorCode::PlatformFailure,
                std::string(operation) + ": unknown failure.");
        }
    }

    TGE::Task<TGE::InputOperationResult>
        ManagerStoppedOperation()
    {
        co_return std::unexpected(MakeInputError(
            TGE::InputErrorCode::ManagerStopped,
            "The input manager has stopped."));
    }

    template<class TValue>
    void InsertUniqueSorted(
        std::vector<TValue>& values,
        TValue value)
    {
        const auto position =
            std::ranges::lower_bound(values, value);
        if (position == values.end() || *position != value)
        {
            values.insert(position, value);
        }
    }

    template<class TValue>
    void EraseSorted(
        std::vector<TValue>& values,
        TValue value)
    {
        const auto position =
            std::ranges::lower_bound(values, value);
        if (position != values.end() && *position == value)
        {
            values.erase(position);
        }
    }

    template<class TCallback>
    struct SubscriptionEntry
    {
        explicit SubscriptionEntry(TCallback callback)
            : callback(std::move(callback))
        {
        }

        void Deactivate() noexcept
        {
            std::scoped_lock lock(callbackMutex);
            active.store(false, std::memory_order_release);
        }

        mutable std::recursive_mutex callbackMutex;
        std::atomic<bool> active { true };
        TCallback callback;
    };

    template<class TCallback>
    using SubscriptionMap =
        std::map<
            std::uint64_t,
            std::shared_ptr<SubscriptionEntry<TCallback>>>;

    template<class TCallback>
    std::vector<std::shared_ptr<SubscriptionEntry<TCallback>>>
        SnapshotSubscriptions(
            const SubscriptionMap<TCallback>& subscriptions)
    {
        std::vector<std::shared_ptr<SubscriptionEntry<TCallback>>> result;
        result.reserve(subscriptions.size());
        for (const auto& [_, subscription] : subscriptions)
        {
            result.emplace_back(subscription);
        }
        return result;
    }

    template<class TCallback, class TEvent>
    void InvokeSubscriptions(
        const std::vector<std::shared_ptr<SubscriptionEntry<TCallback>>>&
            subscriptions,
        const TEvent& event) noexcept
    {
        for (const auto& subscription : subscriptions)
        {
            std::scoped_lock lock(subscription->callbackMutex);
            if (!subscription->active.load(
                    std::memory_order_acquire))
            {
                continue;
            }

            try
            {
                subscription->callback(event);
            }
            catch (...)
            {
                // One application callback cannot disrupt event ordering.
            }
        }
    }
}

namespace TGE::Internal
{
    struct InputManager::State final
        : IInputPlatformEventSink,
          std::enable_shared_from_this<InputManager::State>
    {
        struct Record
        {
            InputContextId id;
            InputContextDescriptor requested;
            InputContextConfiguration configuration;
            InputContextCapabilities capabilities;
            InputContextLifecycleState lifecycle {
                InputContextLifecycleState::Open
            };

            std::vector<PhysicalKeyCode> pressedKeys;
            InputModifiers modifiers;
            InputPoint pointerPosition;
            std::vector<PointerButton> pressedPointerButtons;
            std::map<std::uint64_t, TouchContactState> touches;
            std::shared_ptr<const InputStateSnapshot> snapshot;

            SubscriptionMap<KeyboardInputCallback> keyboard;
            SubscriptionMap<TextInputCallback> text;
            SubscriptionMap<PointerMovedCallback> pointerMoved;
            SubscriptionMap<PointerButtonCallback> pointerButton;
            SubscriptionMap<PointerWheelCallback> pointerWheel;
            SubscriptionMap<TouchInputCallback> touch;
            std::uint64_t nextSubscription { 1 };
            mutable std::mutex mutex;
        };

        class Context final : public IInputContext
        {
        public:
            Context(
                std::weak_ptr<State> state,
                std::shared_ptr<Record> record)
                : state(std::move(state)),
                  record(std::move(record))
            {
            }

            InputContextId Id() const noexcept override
            {
                return record->id;
            }

            InputContextDescriptor RequestedDescriptor() const override
            {
                std::scoped_lock lock(record->mutex);
                return record->requested;
            }

            InputContextConfiguration EffectiveConfiguration()
                const override
            {
                std::scoped_lock lock(record->mutex);
                return record->configuration;
            }

            InputContextCapabilities Capabilities()
                const noexcept override
            {
                std::scoped_lock lock(record->mutex);
                return record->capabilities;
            }

            InputContextLifecycleState LifecycleState()
                const noexcept override
            {
                std::scoped_lock lock(record->mutex);
                return record->lifecycle;
            }

            std::shared_ptr<const InputStateSnapshot>
                StateSnapshot() const override
            {
                std::scoped_lock lock(record->mutex);
                return record->snapshot;
            }

            Task<InputOperationResult> SetEnabledAsync(
                bool enabled) override
            {
                auto owner = state.lock();
                if (!owner)
                {
                    return ManagerStoppedOperation();
                }

                return owner->MutateAsync(
                    record,
                    [enabled](IInputPlatform& platform, InputContextId id)
                    {
                        return platform.SetEnabled(id, enabled);
                    });
            }

            Task<InputOperationResult> SetCaptureAsync(
                bool captured) override
            {
                auto owner = state.lock();
                if (!owner)
                {
                    return ManagerStoppedOperation();
                }

                return owner->MutateAsync(
                    record,
                    [captured](
                        IInputPlatform& platform,
                        InputContextId id)
                    {
                        return platform.SetCapture(id, captured);
                    });
            }

            Task<InputOperationResult> SetRelativePointerModeAsync(
                bool enabled) override
            {
                auto owner = state.lock();
                if (!owner)
                {
                    return ManagerStoppedOperation();
                }

                return owner->MutateAsync(
                    record,
                    [enabled](IInputPlatform& platform, InputContextId id)
                    {
                        return platform.SetRelativePointerMode(
                            id,
                            enabled);
                    });
            }

            InputSubscription SubscribeKeyboard(
                KeyboardInputCallback callback) override
            {
                return Subscribe(
                    &Record::keyboard,
                    std::move(callback));
            }

            InputSubscription SubscribeText(
                TextInputCallback callback) override
            {
                return Subscribe(
                    &Record::text,
                    std::move(callback));
            }

            InputSubscription SubscribePointerMoved(
                PointerMovedCallback callback) override
            {
                return Subscribe(
                    &Record::pointerMoved,
                    std::move(callback));
            }

            InputSubscription SubscribePointerButton(
                PointerButtonCallback callback) override
            {
                return Subscribe(
                    &Record::pointerButton,
                    std::move(callback));
            }

            InputSubscription SubscribePointerWheel(
                PointerWheelCallback callback) override
            {
                return Subscribe(
                    &Record::pointerWheel,
                    std::move(callback));
            }

            InputSubscription SubscribeTouch(
                TouchInputCallback callback) override
            {
                return Subscribe(
                    &Record::touch,
                    std::move(callback));
            }

        private:
            friend struct State;

            template<class TCallback>
            InputSubscription Subscribe(
                SubscriptionMap<TCallback> Record::* member,
                TCallback callback)
            {
                if (!callback)
                {
                    return {};
                }

                std::uint64_t subscriptionId;
                std::shared_ptr<SubscriptionEntry<TCallback>> entry;
                {
                    std::scoped_lock lock(record->mutex);
                    if (record->lifecycle !=
                        InputContextLifecycleState::Open)
                    {
                        return {};
                    }

                    subscriptionId = record->nextSubscription++;
                    entry =
                        std::make_shared<SubscriptionEntry<TCallback>>(
                            std::move(callback));
                    (record.get()->*member).emplace(
                        subscriptionId,
                        entry);
                }

                std::weak_ptr<Record> weakRecord = record;
                return InputSubscription(
                    [weakRecord, member, subscriptionId, entry]
                    {
                        entry->Deactivate();
                        if (auto target = weakRecord.lock())
                        {
                            std::scoped_lock lock(target->mutex);
                            (target.get()->*member).erase(subscriptionId);
                        }
                    });
            }

            std::weak_ptr<State> state;
            std::shared_ptr<Record> record;
        };

        State(
            std::shared_ptr<DesktopEventRuntime> runtime,
            std::unique_ptr<IInputPlatform> platform)
            : runtime(runtime),
              platform(std::move(platform))
        {
        }

        void Initialize()
        {
            platform->SetEventSink(this);
            const auto weak = weak_from_this();
            if (const auto currentRuntime = runtime.lock())
            {
                (void)currentRuntime->Post(
                    [weak]
                    {
                        const auto owner = weak.lock();
                        if (!owner || owner->stopping.load())
                        {
                            return;
                        }
                        owner->DiscoverConnectedDevicesOnRuntime();
                    });
            }
        }

        void DiscoverConnectedDevicesOnRuntime() noexcept
        {
            std::vector<InputDeviceDescriptor> connected;
            try
            {
                connected = platform->ConnectedDevices();
            }
            catch (...)
            {
                // Device discovery is best effort. Hotplug callbacks can still
                // populate the inventory after an enumeration failure.
                return;
            }

            std::scoped_lock lock(mutex);
            if (stopping.load())
            {
                return;
            }
            for (auto& device : connected)
            {
                if (!device.id ||
                    devices.contains(device.id))
                {
                    continue;
                }
                deviceOrder.emplace_back(device.id);
                devices.emplace(device.id, std::move(device));
            }
        }

        std::shared_ptr<Record> FindRecord(
            InputContextId id) const noexcept
        {
            std::scoped_lock lock(mutex);
            const auto found = contexts.find(id);
            if (found == contexts.end())
            {
                return {};
            }
            return found->second->record;
        }

        std::shared_ptr<IInputContext> FindContext(
            InputContextId id) const noexcept
        {
            std::scoped_lock lock(mutex);
            const auto found = contexts.find(id);
            if (found == contexts.end())
            {
                return {};
            }
            return found->second;
        }

        std::vector<std::shared_ptr<IInputContext>>
            ContextSnapshot() const
        {
            std::scoped_lock lock(mutex);
            std::vector<std::shared_ptr<IInputContext>> result;
            result.reserve(contextOrder.size());
            for (const auto id : contextOrder)
            {
                const auto found = contexts.find(id);
                if (found != contexts.end())
                {
                    result.emplace_back(found->second);
                }
            }
            return result;
        }

        std::vector<InputDeviceDescriptor> DeviceSnapshot() const
        {
            std::scoped_lock lock(mutex);
            std::vector<InputDeviceDescriptor> result;
            result.reserve(deviceOrder.size());
            for (const auto id : deviceOrder)
            {
                const auto found = devices.find(id);
                if (found != devices.end())
                {
                    result.emplace_back(found->second);
                }
            }
            return result;
        }

        InputSubscription SubscribeDeviceChanged(
            InputDeviceChangedCallback callback)
        {
            if (!callback || stopping.load())
            {
                return {};
            }

            std::uint64_t subscriptionId;
            auto entry =
                std::make_shared<
                    SubscriptionEntry<InputDeviceChangedCallback>>(
                    std::move(callback));
            {
                std::scoped_lock lock(mutex);
                if (stopping.load())
                {
                    return {};
                }
                subscriptionId = nextDeviceSubscription++;
                deviceChanged.emplace(subscriptionId, entry);
            }

            std::weak_ptr<State> weak = shared_from_this();
            return InputSubscription(
                [weak, subscriptionId, entry]
                {
                    entry->Deactivate();
                    if (auto owner = weak.lock())
                    {
                        std::scoped_lock lock(owner->mutex);
                        owner->deviceChanged.erase(subscriptionId);
                    }
                });
        }

        Task<InputContextResult> CreateContextAsync(
            InputContextDescriptor descriptor,
            InputPlatformTarget target)
        {
            return CreateContextTask(
                shared_from_this(),
                std::move(descriptor),
                target);
        }

        static Task<InputContextResult> CreateContextTask(
            std::shared_ptr<State> self,
            InputContextDescriptor descriptor,
            InputPlatformTarget target)
        {
            if (self->stopping.load())
            {
                co_return std::unexpected(MakeInputError(
                    InputErrorCode::ManagerStopped,
                    "The input manager has stopped."));
            }

            const auto id = InputContextId::FromValue(
                self->nextContextId.fetch_add(1));

            try
            {
                auto runtime = self->runtime.lock();
                if (!runtime)
                {
                    co_return std::unexpected(MakeInputError(
                        InputErrorCode::ManagerStopped,
                        "The desktop event runtime is unavailable."));
                }
                auto submission = runtime->Submit(
                    [self,
                     id,
                     descriptor = std::move(descriptor),
                     target]() mutable
                    {
                        return self->CreateContextOnRuntime(
                            id,
                            std::move(descriptor),
                            target);
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
                    co_return std::unexpected(MakeInputError(
                        InputErrorCode::ManagerStopped,
                        "The desktop event runtime stopped before input "
                        "context creation completed."));
                }
                co_return std::unexpected(
                    CurrentExceptionError("Input context creation failed"));
            }
        }

        template<class TOperation>
        Task<InputOperationResult> MutateAsync(
            std::shared_ptr<Record> record,
            TOperation operation)
        {
            return MutateTask(
                shared_from_this(),
                std::move(record),
                std::move(operation));
        }

        template<class TOperation>
        static Task<InputOperationResult> MutateTask(
            std::shared_ptr<State> self,
            std::shared_ptr<Record> record,
            TOperation operation)
        {
            if (self->stopping.load())
            {
                co_return std::unexpected(MakeInputError(
                    InputErrorCode::ManagerStopped,
                    "The input manager has stopped."));
            }

            try
            {
                auto runtime = self->runtime.lock();
                if (!runtime)
                {
                    co_return std::unexpected(MakeInputError(
                        InputErrorCode::ManagerStopped,
                        "The desktop event runtime is unavailable."));
                }
                auto submission = runtime->Submit(
                    [self,
                     record = std::move(record),
                     operation = std::move(operation)]() mutable
                    {
                        if (self->stopping.load())
                        {
                            return InputOperationResult(
                                std::unexpected(MakeInputError(
                                    InputErrorCode::ManagerStopped,
                                    "The input manager has stopped.")));
                        }
                        return self->MutateOnRuntime(
                            record,
                            std::move(operation));
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
                    co_return std::unexpected(MakeInputError(
                        InputErrorCode::ManagerStopped,
                        "The desktop event runtime stopped before the input "
                        "operation completed."));
                }
                co_return std::unexpected(
                    CurrentExceptionError("Input operation failed"));
            }
        }

        Task<InputOperationResult> DestroyContextAsync(
            InputContextId id)
        {
            return DestroyContextTask(shared_from_this(), id);
        }

        static Task<InputOperationResult> DestroyContextTask(
            std::shared_ptr<State> self,
            InputContextId id)
        {
            if (self->stopping.load())
            {
                co_return std::unexpected(MakeInputError(
                    InputErrorCode::ManagerStopped,
                    "The input manager has stopped."));
            }

            try
            {
                auto runtime = self->runtime.lock();
                if (!runtime)
                {
                    co_return std::unexpected(MakeInputError(
                        InputErrorCode::ManagerStopped,
                        "The desktop event runtime is unavailable."));
                }
                auto submission = runtime->Submit(
                    [self, id]
                    {
                        if (self->stopping.load())
                        {
                            return InputOperationResult(
                                std::unexpected(MakeInputError(
                                    InputErrorCode::ManagerStopped,
                                    "The input manager has stopped.")));
                        }
                        return self->DestroyContextOnRuntime(id);
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
                    co_return std::unexpected(MakeInputError(
                        InputErrorCode::ManagerStopped,
                        "The desktop event runtime stopped before input "
                        "context destruction completed."));
                }
                co_return std::unexpected(
                    CurrentExceptionError("Input context destruction failed"));
            }
        }

        Task<void> ShutdownAsync()
        {
            return ShutdownTask(shared_from_this());
        }

        static Task<void> ShutdownTask(std::shared_ptr<State> self)
        {
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
                        if (!self->stopping.exchange(true))
                        {
                            self->CleanupOnRuntime();
                        }
                    });
                runtime.reset();
                co_await std::move(submission);
            }
            catch (...)
            {
                // Sink detachment is thread-safe. If the runtime has already
                // stopped, thread-affine cleanup cannot be recovered, but
                // every retained public facade still becomes terminal.
                if (!self->stopping.exchange(true))
                {
                    self->platform->SetEventSink(nullptr);
                    self->MarkAllDestroyed();
                    self->DeactivateManagerSubscriptions();
                }
            }
        }

        void Shutdown() noexcept
        {
            if (stopping.load())
            {
                return;
            }

            const auto currentRuntime = runtime.lock();
            if (currentRuntime &&
                currentRuntime->IsEventThread())
            {
                try
                {
                    auto completion =
                        Execution::SyncWait(ShutdownAsync());
                    (void)completion;
                    return;
                }
                catch (...)
                {
                    // Continue into the nonblocking fallback.
                }
            }

            ShutdownFallback();
        }

        void ShutdownFallback() noexcept
        {
            if (stopping.exchange(true))
            {
                return;
            }

            try
            {
                // Detachment is the SPI's thread-safe quiescence boundary.
                // Once it returns, no callback can retain this raw sink.
                platform->SetEventSink(nullptr);

                auto contextIds = DetachAllContexts();
                DeactivateManagerSubscriptions();

                auto cleanup =
                    [
                        platform = platform,
                        contextIds = std::move(contextIds)
                    ]() noexcept
                    {
                        for (const auto id : contextIds)
                        {
                            try
                            {
                                (void)platform->DestroyContext(id);
                            }
                            catch (...)
                            {
                            }
                        }
                        platform->Shutdown();
                    };

                const auto currentRuntime = runtime.lock();
                if (currentRuntime &&
                    currentRuntime->IsEventThread())
                {
                    cleanup();
                }
                else if (!currentRuntime ||
                    !currentRuntime->Post(std::move(cleanup)))
                {
                    // The event runtime has already closed admission. Public
                    // state is still safely detached; the adapter destructor
                    // remains the last best-effort cleanup boundary.
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
                MarkAllDestroyed();
                DeactivateManagerSubscriptions();
            }
        }

        void OnKeyboard(
            InputPlatformKeyboardEvent event) noexcept override
        {
            QueuePlatformEvent(
                [event = std::move(event)](State& self) mutable
                {
                    self.PublishKeyboard(std::move(event));
                });
        }

        void OnText(
            InputPlatformTextEvent event) noexcept override
        {
            QueuePlatformEvent(
                [event = std::move(event)](State& self) mutable
                {
                    self.PublishText(std::move(event));
                });
        }

        void OnPointerMoved(
            InputPlatformPointerMovedEvent event) noexcept override
        {
            QueuePlatformEvent(
                [event = std::move(event)](State& self) mutable
                {
                    self.PublishPointerMoved(std::move(event));
                });
        }

        void OnPointerButton(
            InputPlatformPointerButtonEvent event) noexcept override
        {
            QueuePlatformEvent(
                [event = std::move(event)](State& self) mutable
                {
                    self.PublishPointerButton(std::move(event));
                });
        }

        void OnPointerWheel(
            InputPlatformPointerWheelEvent event) noexcept override
        {
            QueuePlatformEvent(
                [event = std::move(event)](State& self) mutable
                {
                    self.PublishPointerWheel(std::move(event));
                });
        }

        void OnTouch(
            InputPlatformTouchEvent event) noexcept override
        {
            QueuePlatformEvent(
                [event = std::move(event)](State& self) mutable
                {
                    self.PublishTouch(std::move(event));
                });
        }

        void OnTargetInputChanged(
            InputContextId context,
            InputContextConfiguration configuration,
            bool inputInvalidated) noexcept override
        {
            try
            {
                auto self = shared_from_this();
                const auto currentRuntime = runtime.lock();
                if (!currentRuntime)
                {
                    return;
                }
                if (currentRuntime->IsEventThread())
                {
                    self->ApplyTargetConfiguration(
                        context,
                        std::move(configuration),
                        inputInvalidated);
                    return;
                }

                (void)currentRuntime->Post(
                    [
                        self = std::move(self),
                        context,
                        configuration = std::move(configuration),
                        inputInvalidated
                    ]() mutable
                    {
                        if (!self->stopping.load())
                        {
                            self->ApplyTargetConfiguration(
                                context,
                                std::move(configuration),
                                inputInvalidated);
                        }
                    });
            }
            catch (...)
            {
            }
        }

        void OnDeviceChanged(
            InputDeviceChangeKind change,
            InputDeviceDescriptor device) noexcept override
        {
            QueuePlatformEvent(
                [change, device = std::move(device)](State& self) mutable
                {
                    self.PublishDeviceChanged(
                        change,
                        std::move(device));
                });
        }

    private:
        template<class TOperation>
        void QueuePlatformEvent(TOperation operation) noexcept
        {
            try
            {
                auto self = shared_from_this();
                auto invoke =
                    [self, operation = std::move(operation)]() mutable
                    {
                        if (!self->stopping.load())
                        {
                            operation(*self);
                        }
                    };

                // Always defer platform callbacks, even when the adapter is
                // already on the event thread. This keeps application code
                // outside a native callback stack, preserves FIFO routing, and
                // makes event-sink detachment a tractable quiescence boundary.
                if (const auto currentRuntime = runtime.lock())
                {
                    (void)currentRuntime->Post(std::move(invoke));
                }
            }
            catch (...)
            {
                // Platform callbacks cross a noexcept fault boundary.
            }
        }

        InputContextResult CreateContextOnRuntime(
            InputContextId id,
            InputContextDescriptor descriptor,
            InputPlatformTarget target)
        {
            if (stopping.load())
            {
                return std::unexpected(MakeInputError(
                    InputErrorCode::ManagerStopped,
                    "The input manager has stopped."));
            }

            auto creation =
                platform->CreateContext(id, descriptor, target);
            if (!creation)
            {
                return std::unexpected(std::move(creation.error()));
            }

            auto record = std::make_shared<Record>();
            record->id = id;
            record->requested = std::move(descriptor);
            record->configuration =
                std::move(creation->configuration);
            record->capabilities = creation->capabilities;
            PublishSnapshotLocked(*record, 0, {});

            auto context = std::make_shared<Context>(
                weak_from_this(),
                record);
            {
                std::scoped_lock lock(mutex);
                if (stopping.load())
                {
                    (void)platform->DestroyContext(id);
                    return std::unexpected(MakeInputError(
                        InputErrorCode::ManagerStopped,
                        "The input manager stopped during context creation."));
                }
                contexts.emplace(id, context);
                contextOrder.emplace_back(id);
            }

            return std::shared_ptr<IInputContext>(std::move(context));
        }

        template<class TOperation>
        InputOperationResult MutateOnRuntime(
            const std::shared_ptr<Record>& record,
            TOperation operation)
        {
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle !=
                    InputContextLifecycleState::Open)
                {
                    return std::unexpected(MakeInputError(
                        InputErrorCode::ContextDestroyed,
                        std::format(
                            "Input context {} is destroyed.",
                            record->id.Value())));
                }
            }

            auto mutation = operation(*platform, record->id);
            if (!mutation)
            {
                return std::unexpected(std::move(mutation.error()));
            }

            {
                std::scoped_lock lock(record->mutex);
                const bool disabled =
                    record->configuration.enabled &&
                    !mutation->configuration.enabled;
                record->configuration =
                    std::move(mutation->configuration);
                if (disabled)
                {
                    record->pressedKeys.clear();
                    record->modifiers = {};
                    record->pressedPointerButtons.clear();
                    record->touches.clear();
                }

                const auto sequence =
                    record->snapshot
                        ? record->snapshot->Sequence()
                        : 0;
                const auto timestamp =
                    record->snapshot
                        ? record->snapshot->Timestamp()
                        : InputTimestamp {};
                PublishSnapshotLocked(
                    *record,
                    sequence,
                    timestamp);
            }

            return mutation->status;
        }

        InputOperationResult DestroyContextOnRuntime(
            InputContextId id)
        {
            std::shared_ptr<Context> context;
            {
                std::scoped_lock lock(mutex);
                const auto found = contexts.find(id);
                if (found == contexts.end())
                {
                    return std::unexpected(MakeInputError(
                        InputErrorCode::ContextNotFound,
                        std::format(
                            "Input context {} was not found.",
                            id.Value())));
                }
                context = found->second;
            }

            const auto record = context->record;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle ==
                    InputContextLifecycleState::Destroyed)
                {
                    return std::unexpected(MakeInputError(
                        InputErrorCode::ContextDestroyed,
                        std::format(
                            "Input context {} is already destroyed.",
                            id.Value())));
                }
                record->lifecycle =
                    InputContextLifecycleState::Destroying;
            }

            auto result = platform->DestroyContext(id);
            if (!result)
            {
                std::scoped_lock lock(record->mutex);
                record->lifecycle =
                    InputContextLifecycleState::Open;
                return result;
            }

            FinalizeDestroyed(context);
            return *result;
        }

        std::vector<InputContextId> DetachAllContexts()
        {
            std::vector<std::shared_ptr<Context>> owned;
            std::vector<InputContextId> ids;
            {
                std::scoped_lock lock(mutex);
                owned.reserve(contextOrder.size());
                ids.reserve(contextOrder.size());
                for (auto id = contextOrder.rbegin();
                     id != contextOrder.rend();
                     ++id)
                {
                    const auto found = contexts.find(*id);
                    if (found != contexts.end())
                    {
                        owned.emplace_back(found->second);
                        ids.emplace_back(*id);
                    }
                }
                contexts.clear();
                contextOrder.clear();
            }

            for (const auto& context : owned)
            {
                MarkRecordDestroyed(*context->record);
            }
            return ids;
        }

        void CleanupOnRuntime() noexcept
        {
            // Detach before native destruction so a backend reporting teardown
            // events cannot re-enter the facade.
            platform->SetEventSink(nullptr);
            try
            {
                const auto contextIds = DetachAllContexts();
                DeactivateManagerSubscriptions();
                for (const auto id : contextIds)
                {
                    try
                    {
                        (void)platform->DestroyContext(id);
                    }
                    catch (...)
                    {
                    }
                }
            }
            catch (...)
            {
                MarkAllDestroyed();
                DeactivateManagerSubscriptions();
            }
            platform->Shutdown();
        }

        void MarkAllDestroyed() noexcept
        {
            std::vector<std::shared_ptr<Context>> owned;
            {
                std::scoped_lock lock(mutex);
                for (auto& [_, context] : contexts)
                {
                    owned.emplace_back(context);
                }
                contexts.clear();
                contextOrder.clear();
            }

            for (const auto& context : owned)
            {
                MarkRecordDestroyed(*context->record);
            }
        }

        void DeactivateManagerSubscriptions() noexcept
        {
            SubscriptionMap<InputDeviceChangedCallback> subscriptions;
            {
                std::scoped_lock lock(mutex);
                subscriptions.swap(deviceChanged);
                devices.clear();
                deviceOrder.clear();
            }
            for (auto& [_, subscription] : subscriptions)
            {
                subscription->Deactivate();
            }
        }

        void FinalizeDestroyed(
            const std::shared_ptr<Context>& context) noexcept
        {
            {
                std::scoped_lock lock(mutex);
                contexts.erase(context->record->id);
                std::erase(
                    contextOrder,
                    context->record->id);
            }
            MarkRecordDestroyed(*context->record);
        }

        static void MarkRecordDestroyed(Record& record) noexcept
        {
            SubscriptionMap<KeyboardInputCallback> keyboard;
            SubscriptionMap<TextInputCallback> text;
            SubscriptionMap<PointerMovedCallback> pointerMoved;
            SubscriptionMap<PointerButtonCallback> pointerButton;
            SubscriptionMap<PointerWheelCallback> pointerWheel;
            SubscriptionMap<TouchInputCallback> touch;
            {
                std::scoped_lock lock(record.mutex);
                record.lifecycle =
                    InputContextLifecycleState::Destroyed;
                record.pressedKeys.clear();
                record.modifiers = {};
                record.pressedPointerButtons.clear();
                record.touches.clear();

                try
                {
                    const auto sequence =
                        record.snapshot
                            ? record.snapshot->Sequence()
                            : 0;
                    const auto timestamp =
                        record.snapshot
                            ? record.snapshot->Timestamp()
                            : InputTimestamp {};
                    PublishSnapshotLocked(record, sequence, timestamp);
                }
                catch (...)
                {
                    // Lifecycle teardown remains noexcept. Any retained prior
                    // snapshot is still immutable even if allocation fails.
                }

                keyboard.swap(record.keyboard);
                text.swap(record.text);
                pointerMoved.swap(record.pointerMoved);
                pointerButton.swap(record.pointerButton);
                pointerWheel.swap(record.pointerWheel);
                touch.swap(record.touch);
            }

            auto deactivate =
                [](auto& subscriptions)
                {
                    for (auto& [_, subscription] : subscriptions)
                    {
                        subscription->Deactivate();
                    }
                };
            deactivate(keyboard);
            deactivate(text);
            deactivate(pointerMoved);
            deactivate(pointerButton);
            deactivate(pointerWheel);
            deactivate(touch);
        }

        InputEventMetadata NextMetadata(
            InputContextId context,
            InputDeviceId device)
        {
            auto timestamp = std::chrono::steady_clock::now();
            if (timestamp <= lastTimestamp)
            {
                timestamp = lastTimestamp +
                    std::chrono::nanoseconds(1);
            }
            lastTimestamp = timestamp;

            return InputEventMetadata {
                .context = context,
                .device = device,
                .sequence = nextEventSequence++,
                .timestamp = timestamp
            };
        }

        void PublishKeyboard(InputPlatformKeyboardEvent event)
        {
            const auto record = FindRecord(event.context);
            if (!record)
            {
                return;
            }

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<KeyboardInputCallback>>>
                callbacks;
            KeyboardInputEvent published;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle !=
                        InputContextLifecycleState::Open ||
                    !record->configuration.enabled)
                {
                    return;
                }

                const auto metadata =
                    NextMetadata(event.context, event.device);
                if (event.physicalCode &&
                    event.action == InputAction::Pressed)
                {
                    InsertUniqueSorted(
                        record->pressedKeys,
                        event.physicalCode);
                }
                else if (event.physicalCode)
                {
                    EraseSorted(
                        record->pressedKeys,
                        event.physicalCode);
                }
                record->modifiers = event.modifiers;
                PublishSnapshotLocked(
                    *record,
                    metadata.sequence,
                    metadata.timestamp);
                callbacks =
                    SnapshotSubscriptions(record->keyboard);
                published = KeyboardInputEvent {
                    .metadata = metadata,
                    .physicalCode = event.physicalCode,
                    .logicalName = std::move(event.logicalName),
                    .action = event.action,
                    .modifiers = event.modifiers,
                    .repeat = event.repeat
                };
            }

            InvokeSubscriptions(callbacks, published);
        }

        void ApplyTargetConfiguration(
            InputContextId context,
            InputContextConfiguration configuration,
            bool inputInvalidated)
        {
            const auto record = FindRecord(context);
            if (!record)
            {
                return;
            }

            std::scoped_lock lock(record->mutex);
            if (record->lifecycle !=
                InputContextLifecycleState::Open)
            {
                return;
            }

            record->configuration = std::move(configuration);
            if (inputInvalidated)
            {
                record->pressedKeys.clear();
                record->modifiers = {};
                record->pressedPointerButtons.clear();
                record->touches.clear();
            }
            const auto sequence =
                record->snapshot
                    ? record->snapshot->Sequence()
                    : 0;
            const auto timestamp =
                record->snapshot
                    ? record->snapshot->Timestamp()
                    : InputTimestamp {};
            PublishSnapshotLocked(
                *record,
                sequence,
                timestamp);
        }

        void PublishText(InputPlatformTextEvent event)
        {
            const auto record = FindRecord(event.context);
            if (!record)
            {
                return;
            }

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<TextInputCallback>>>
                callbacks;
            TextInputEvent published;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle !=
                        InputContextLifecycleState::Open ||
                    !record->configuration.enabled)
                {
                    return;
                }
                const auto metadata =
                    NextMetadata(event.context, event.device);
                PublishSnapshotLocked(
                    *record,
                    metadata.sequence,
                    metadata.timestamp);
                callbacks =
                    SnapshotSubscriptions(record->text);
                published = TextInputEvent {
                    .metadata = metadata,
                    .text = std::move(event.text)
                };
            }
            InvokeSubscriptions(callbacks, published);
        }

        void PublishPointerMoved(
            InputPlatformPointerMovedEvent event)
        {
            const auto record = FindRecord(event.context);
            if (!record)
            {
                return;
            }

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<PointerMovedCallback>>>
                callbacks;
            PointerMovedEvent published;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle !=
                        InputContextLifecycleState::Open ||
                    !record->configuration.enabled)
                {
                    return;
                }
                const auto metadata =
                    NextMetadata(event.context, event.device);
                record->pointerPosition = event.position;
                PublishSnapshotLocked(
                    *record,
                    metadata.sequence,
                    metadata.timestamp);
                callbacks =
                    SnapshotSubscriptions(record->pointerMoved);
                published = PointerMovedEvent {
                    .metadata = metadata,
                    .position = event.position,
                    .delta = event.delta,
                    .relative = event.relative
                };
            }
            InvokeSubscriptions(callbacks, published);
        }

        void PublishPointerButton(
            InputPlatformPointerButtonEvent event)
        {
            const auto record = FindRecord(event.context);
            if (!record)
            {
                return;
            }

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<PointerButtonCallback>>>
                callbacks;
            PointerButtonEvent published;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle !=
                        InputContextLifecycleState::Open ||
                    !record->configuration.enabled)
                {
                    return;
                }
                const auto metadata =
                    NextMetadata(event.context, event.device);
                record->pointerPosition = event.position;
                if (event.action == InputAction::Pressed)
                {
                    InsertUniqueSorted(
                        record->pressedPointerButtons,
                        event.button);
                }
                else
                {
                    EraseSorted(
                        record->pressedPointerButtons,
                        event.button);
                }
                PublishSnapshotLocked(
                    *record,
                    metadata.sequence,
                    metadata.timestamp);
                callbacks =
                    SnapshotSubscriptions(record->pointerButton);
                published = PointerButtonEvent {
                    .metadata = metadata,
                    .button = event.button,
                    .action = event.action,
                    .position = event.position
                };
            }
            InvokeSubscriptions(callbacks, published);
        }

        void PublishPointerWheel(
            InputPlatformPointerWheelEvent event)
        {
            const auto record = FindRecord(event.context);
            if (!record)
            {
                return;
            }

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<PointerWheelCallback>>>
                callbacks;
            PointerWheelEvent published;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle !=
                        InputContextLifecycleState::Open ||
                    !record->configuration.enabled)
                {
                    return;
                }
                const auto metadata =
                    NextMetadata(event.context, event.device);
                PublishSnapshotLocked(
                    *record,
                    metadata.sequence,
                    metadata.timestamp);
                callbacks =
                    SnapshotSubscriptions(record->pointerWheel);
                published = PointerWheelEvent {
                    .metadata = metadata,
                    .delta = event.delta,
                    .unit = event.unit
                };
            }
            InvokeSubscriptions(callbacks, published);
        }

        void PublishTouch(InputPlatformTouchEvent event)
        {
            const auto record = FindRecord(event.context);
            if (!record)
            {
                return;
            }

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<TouchInputCallback>>>
                callbacks;
            TouchInputEvent published;
            {
                std::scoped_lock lock(record->mutex);
                if (record->lifecycle !=
                        InputContextLifecycleState::Open ||
                    !record->configuration.enabled)
                {
                    return;
                }
                const auto metadata =
                    NextMetadata(event.context, event.device);
                if (event.action == TouchAction::Ended ||
                    event.action == TouchAction::Cancelled)
                {
                    record->touches.erase(event.contact);
                }
                else
                {
                    record->touches.insert_or_assign(
                        event.contact,
                        TouchContactState {
                            .contact = event.contact,
                            .position = event.position,
                            .pressure = event.pressure
                        });
                }
                PublishSnapshotLocked(
                    *record,
                    metadata.sequence,
                    metadata.timestamp);
                callbacks =
                    SnapshotSubscriptions(record->touch);
                published = TouchInputEvent {
                    .metadata = metadata,
                    .contact = event.contact,
                    .action = event.action,
                    .position = event.position,
                    .pressure = event.pressure
                };
            }
            InvokeSubscriptions(callbacks, published);
        }

        void PublishDeviceChanged(
            InputDeviceChangeKind change,
            InputDeviceDescriptor device)
        {
            if (!device.id)
            {
                return;
            }

            std::vector<
                std::shared_ptr<
                    SubscriptionEntry<InputDeviceChangedCallback>>>
                callbacks;
            InputDeviceChangedEvent published;
            {
                std::scoped_lock lock(mutex);
                if (change == InputDeviceChangeKind::Disconnected)
                {
                    const auto found = devices.find(device.id);
                    if (found == devices.end())
                    {
                        return;
                    }
                    device = found->second;
                    devices.erase(found);
                    std::erase(deviceOrder, device.id);
                }
                else
                {
                    const auto existing = devices.find(device.id);
                    if (change == InputDeviceChangeKind::Connected &&
                        existing != devices.end() &&
                        existing->second == device)
                    {
                        return;
                    }
                    const bool inserted =
                        existing == devices.end();
                    devices.insert_or_assign(device.id, device);
                    if (inserted)
                    {
                        deviceOrder.emplace_back(device.id);
                        if (change == InputDeviceChangeKind::Updated)
                        {
                            change = InputDeviceChangeKind::Connected;
                        }
                    }
                    else if (
                        change == InputDeviceChangeKind::Connected)
                    {
                        change = InputDeviceChangeKind::Updated;
                    }
                }

                const auto metadata = NextMetadata({}, device.id);
                callbacks =
                    SnapshotSubscriptions(deviceChanged);
                published = InputDeviceChangedEvent {
                    .sequence = metadata.sequence,
                    .timestamp = metadata.timestamp,
                    .change = change,
                    .device = std::move(device)
                };
            }
            InvokeSubscriptions(callbacks, published);
        }

        static void PublishSnapshotLocked(
            Record& record,
            InputEventSequence sequence,
            InputTimestamp timestamp)
        {
            std::vector<TouchContactState> touches;
            touches.reserve(record.touches.size());
            for (const auto& [_, touch] : record.touches)
            {
                touches.emplace_back(touch);
            }

            record.snapshot =
                std::make_shared<const InputStateSnapshot>(
                    record.id,
                    sequence,
                    timestamp,
                    record.configuration,
                    record.pressedKeys,
                    record.modifiers,
                    record.pointerPosition,
                    record.pressedPointerButtons,
                    std::move(touches));
        }

        std::weak_ptr<DesktopEventRuntime> runtime;
        std::shared_ptr<IInputPlatform> platform;

        mutable std::mutex mutex;
        std::unordered_map<InputContextId, std::shared_ptr<Context>>
            contexts;
        std::vector<InputContextId> contextOrder;
        std::unordered_map<InputDeviceId, InputDeviceDescriptor>
            devices;
        std::vector<InputDeviceId> deviceOrder;
        SubscriptionMap<InputDeviceChangedCallback> deviceChanged;

        std::atomic<std::uint64_t> nextContextId { 1 };
        std::uint64_t nextDeviceSubscription { 1 };
        InputEventSequence nextEventSequence { 1 };
        InputTimestamp lastTimestamp {};
        std::atomic<bool> stopping { false };
    };

    InputManager::InputManager(
        std::shared_ptr<DesktopEventRuntime> runtime,
        std::unique_ptr<IInputPlatform> platform)
    {
        if (!runtime)
        {
            throw std::invalid_argument(
                "InputManager requires a desktop event runtime.");
        }
        if (!platform)
        {
            throw std::invalid_argument(
                "InputManager requires an input platform.");
        }

        state = std::make_shared<State>(
            std::move(runtime),
            std::move(platform));
        state->Initialize();
    }

    InputManager::~InputManager()
    {
        state->Shutdown();
    }

    Task<InputContextResult> InputManager::CreateContextAsync(
        InputContextDescriptor descriptor)
    {
        return state->CreateContextAsync(
            std::move(descriptor),
            {});
    }

    Task<InputContextResult>
        InputManager::CreateContextForTargetAsync(
            InputContextDescriptor descriptor,
            InputPlatformTarget target)
    {
        return state->CreateContextAsync(
            std::move(descriptor),
            target);
    }

    std::shared_ptr<IInputContext> InputManager::FindContext(
        InputContextId id) const noexcept
    {
        return state->FindContext(id);
    }

    std::vector<std::shared_ptr<IInputContext>>
        InputManager::Contexts() const
    {
        return state->ContextSnapshot();
    }

    Task<InputOperationResult> InputManager::DestroyContextAsync(
        InputContextId id)
    {
        return state->DestroyContextAsync(id);
    }

    std::vector<InputDeviceDescriptor>
        InputManager::Devices() const
    {
        return state->DeviceSnapshot();
    }

    InputSubscription InputManager::SubscribeDeviceChanged(
        InputDeviceChangedCallback callback)
    {
        return state->SubscribeDeviceChanged(std::move(callback));
    }

    Task<void> InputManager::ShutdownAsync()
    {
        return state->ShutdownAsync();
    }
}
