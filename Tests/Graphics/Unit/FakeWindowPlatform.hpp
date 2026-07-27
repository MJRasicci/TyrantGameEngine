#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FakeDesktopEventPump.hpp"
#include "Internal/Graphics/IWindowPlatform.hpp"

namespace TGE::Tests
{
    struct FakeWindowPlatformLifecycle final
    {
        mutable std::mutex mutex;
        std::vector<WindowId> destroyedWindows;
        std::vector<std::thread::id> cleanupThreads;
        std::size_t shutdownCount { 0 };
        bool shutdownWithLiveWindows { false };
    };

    class FakeWindowPlatform final : public Internal::IWindowPlatform
    {
    public:
        explicit FakeWindowPlatform(
            FakeDesktopEventPump& pump,
            std::shared_ptr<std::atomic<std::size_t>>
                destructionCount = {},
            std::shared_ptr<FakeWindowPlatformLifecycle>
                lifecycle = {})
            : pump(pump),
              destructionCount(std::move(destructionCount)),
              lifecycle(std::move(lifecycle))
        {
            capabilities = WindowCapabilities {
                .decorations = true,
                .resizing = true,
                .minimizing = true,
                .maximizing = true,
                .fullscreen = true,
                .positioning = true,
                .visibility = true,
                .focus = true,
                .inputControl = true,
                .alwaysOnTop = true,
                .parentWindows = true,
                .childWindows = true,
                .toolWindows = true,
                .dialogWindows = true,
                .independentAxisScale = true,
                .applicationModality = true
            };
        }

        ~FakeWindowPlatform() override
        {
            if (destructionCount)
            {
                destructionCount->fetch_add(1);
            }
        }

        void SetEventSink(
            Internal::IWindowPlatformEventSink* eventSink) noexcept override
        {
            std::scoped_lock lock(sinkMutex);
            sink = eventSink;
        }

        void Shutdown() noexcept override
        {
            std::size_t liveWindows;
            {
                std::scoped_lock lock(mutex);
                liveWindows = windows.size();
            }
            if (lifecycle)
            {
                std::scoped_lock lock(lifecycle->mutex);
                ++lifecycle->shutdownCount;
                lifecycle->shutdownWithLiveWindows =
                    lifecycle->shutdownWithLiveWindows ||
                    liveWindows != 0;
                lifecycle->cleanupThreads.emplace_back(
                    std::this_thread::get_id());
            }
            alive->store(false);
        }

        Internal::WindowPlatformCreateResult CreateWindow(
            WindowId id,
            const WindowDescriptor& descriptor) override
        {
            RecordPlatformCall();
            if (auto failure = TakeFailure())
            {
                return std::unexpected(std::move(*failure));
            }

            WindowConfiguration configuration {
                .title = descriptor.title,
                .role = descriptor.role,
                .parent = descriptor.parent,
                .geometry = WindowGeometry {
                    .logicalBounds = descriptor.bounds,
                    .framebufferSize = ToFramebuffer(
                        descriptor.bounds.size,
                        initialScale),
                    .scale = initialScale
                },
                .state = descriptor.state,
                .chrome = descriptor.chrome,
                .modality = descriptor.modality,
                .visible = descriptor.initiallyVisible,
                .alwaysOnTop = descriptor.alwaysOnTop,
                .inputEnabled = descriptor.acceptsInput,
                .focused = false
            };

            if (!capabilities.positioning)
            {
                configuration.geometry.logicalBounds.position = {};
            }
            if (!capabilities.resizing)
            {
                configuration.chrome.resizable = false;
            }
            if (!capabilities.decorations)
            {
                configuration.chrome.decorations = false;
            }
            if (!capabilities.minimizing)
            {
                configuration.chrome.minimizable = false;
            }
            if (!capabilities.maximizing)
            {
                configuration.chrome.maximizable = false;
            }
            if (configuration.state == WindowState::Fullscreen &&
                !capabilities.fullscreen)
            {
                configuration.state = WindowState::Normal;
            }
            if (configuration.alwaysOnTop && !capabilities.alwaysOnTop)
            {
                configuration.alwaysOnTop = false;
            }
            if (configuration.modality ==
                    WindowModality::ApplicationModal &&
                !capabilities.applicationModality &&
                !capabilities.inputControl)
            {
                configuration.modality = WindowModality::Modeless;
            }

            {
                std::scoped_lock lock(mutex);
                windows[id] = configuration;
            }
            return Internal::WindowPlatformState {
                .configuration = std::move(configuration),
                .capabilities = capabilities
            };
        }

        WindowOperationResult DestroyWindow(WindowId id) override
        {
            RecordPlatformCall();
            if (auto failure = TakeFailure())
            {
                return std::unexpected(std::move(*failure));
            }

            {
                std::scoped_lock lock(mutex);
                if (windows.erase(id) == 0)
                {
                    return DestroyedError();
                }
            }
            if (lifecycle)
            {
                std::scoped_lock lock(lifecycle->mutex);
                lifecycle->destroyedWindows.emplace_back(id);
                lifecycle->cleanupThreads.emplace_back(
                    std::this_thread::get_id());
            }
            std::scoped_lock sinkLock(sinkMutex);
            if (sink)
            {
                sink->OnPlatformClosed(
                    id,
                    WindowCloseReason::ApplicationRequest);
            }
            return WindowOperationStatus::Applied;
        }

        Internal::WindowPlatformMutationResult SetTitle(
            WindowId id,
            std::string title) override
        {
            return Mutate(
                id,
                [title = std::move(title)](
                    WindowConfiguration& configuration) mutable
                {
                    configuration.title = std::move(title);
                    return WindowOperationStatus::Applied;
                });
        }

        Internal::WindowPlatformMutationResult SetLogicalBounds(
            WindowId id,
            LogicalBounds bounds) override
        {
            return Mutate(
                id,
                [this, bounds](WindowConfiguration& configuration)
                {
                    auto status = WindowOperationStatus::Applied;
                    if (capabilities.positioning)
                    {
                        configuration.geometry.logicalBounds.position =
                            bounds.position;
                    }
                    else if (configuration.geometry.logicalBounds.position !=
                             bounds.position)
                    {
                        status = WindowOperationStatus::Normalized;
                    }

                    if (capabilities.resizing)
                    {
                        configuration.geometry.logicalBounds.size =
                            bounds.size;
                        configuration.geometry.framebufferSize =
                            ToFramebuffer(
                                bounds.size,
                                configuration.geometry.scale);
                    }
                    else if (configuration.geometry.logicalBounds.size !=
                             bounds.size)
                    {
                        status = WindowOperationStatus::Normalized;
                    }
                    return status;
                });
        }

        Internal::WindowPlatformMutationResult SetState(
            WindowId id,
            WindowState state) override
        {
            if ((state == WindowState::Fullscreen &&
                 !capabilities.fullscreen) ||
                (state == WindowState::Minimized &&
                 !capabilities.minimizing) ||
                (state == WindowState::Maximized &&
                 !capabilities.maximizing))
            {
                RecordPlatformCall();
                return std::unexpected(UnsupportedError());
            }
            return Mutate(
                id,
                [state](WindowConfiguration& configuration)
                {
                    configuration.state = state;
                    return WindowOperationStatus::Applied;
                });
        }

        Internal::WindowPlatformMutationResult SetVisible(
            WindowId id,
            bool visible) override
        {
            if (!capabilities.visibility)
            {
                RecordPlatformCall();
                return std::unexpected(UnsupportedError());
            }
            return Mutate(
                id,
                [visible](WindowConfiguration& configuration)
                {
                    configuration.visible = visible;
                    return WindowOperationStatus::Applied;
                });
        }

        Internal::WindowPlatformMutationResult RequestFocus(
            WindowId id) override
        {
            if (!capabilities.focus)
            {
                RecordPlatformCall();
                return std::unexpected(UnsupportedError());
            }
            return Mutate(
                id,
                [](WindowConfiguration& configuration)
                {
                    configuration.focused = true;
                    return WindowOperationStatus::Applied;
                });
        }

        Internal::WindowPlatformMutationResult SetInputEnabled(
            WindowId id,
            bool enabled) override
        {
            if (!capabilities.inputControl)
            {
                RecordPlatformCall();
                return std::unexpected(UnsupportedError());
            }
            return Mutate(
                id,
                [enabled](WindowConfiguration& configuration)
                {
                    configuration.inputEnabled = enabled;
                    return WindowOperationStatus::Applied;
                });
        }

        WindowOperationResult RequestClose(WindowId id) override
        {
            RecordPlatformCall();
            if (auto failure = TakeFailure())
            {
                return std::unexpected(std::move(*failure));
            }

            {
                std::scoped_lock lock(mutex);
                if (!windows.contains(id))
                {
                    return DestroyedError();
                }
            }
            std::scoped_lock sinkLock(sinkMutex);
            if (!sink ||
                !sink->OnPlatformCloseRequested(
                    id,
                    WindowCloseReason::ApplicationRequest))
            {
                return WindowOperationStatus::Cancelled;
            }

            {
                std::scoped_lock lock(mutex);
                windows.erase(id);
            }
            sink->OnPlatformClosed(
                id,
                WindowCloseReason::ApplicationRequest);
            return WindowOperationStatus::Applied;
        }

        void QueueConfiguration(
            WindowId id,
            WindowConfiguration configuration)
        {
            QueueEvent(QueuedEvent {
                    .type = EventType::Configuration,
                    .id = id,
                    .configuration = std::move(configuration)
                });
        }

        void QueueConfigurations(
            WindowId id,
            std::vector<WindowConfiguration> configurations)
        {
            std::vector<QueuedEvent> events;
            events.reserve(configurations.size());
            for (auto& configuration : configurations)
            {
                events.emplace_back(QueuedEvent {
                        .type = EventType::Configuration,
                        .id = id,
                        .configuration = std::move(configuration)
                    });
            }
            QueueEvents(std::move(events));
        }

        void QueueCloseRequest(
            WindowId id,
            WindowCloseReason reason = WindowCloseReason::UserRequest)
        {
            QueueEvent(QueuedEvent {
                    .type = EventType::CloseRequest,
                    .id = id,
                    .closeReason = reason
                });
        }

        [[nodiscard]] std::optional<WindowConfiguration> Configuration(
            WindowId id) const
        {
            std::scoped_lock lock(mutex);
            const auto found = windows.find(id);
            if (found == windows.end())
            {
                return {};
            }
            return found->second;
        }

        void FailNext(WindowError error)
        {
            std::scoped_lock lock(mutex);
            nextFailure = std::move(error);
        }

        void SetCallDelay(std::chrono::milliseconds delay) noexcept
        {
            callDelay = delay;
        }

        [[nodiscard]] std::vector<std::thread::id>
            PlatformCallThreads() const
        {
            std::scoped_lock lock(mutex);
            return platformCallThreads;
        }

        [[nodiscard]] std::size_t MaximumConcurrentCalls() const noexcept
        {
            return maximumConcurrentCalls.load();
        }

        WindowCapabilities capabilities;
        WindowScale initialScale { 1.0F, 1.0F };

    private:
        enum class EventType
        {
            Configuration,
            CloseRequest
        };

        struct QueuedEvent
        {
            EventType type { EventType::Configuration };
            WindowId id;
            WindowConfiguration configuration;
            WindowCloseReason closeReason {
                WindowCloseReason::PlatformRequest
            };
        };

        class ActiveCall final
        {
        public:
            explicit ActiveCall(FakeWindowPlatform& platform)
                : platform(platform)
            {
                const auto active =
                    platform.activeCalls.fetch_add(1) + 1;
                auto maximum =
                    platform.maximumConcurrentCalls.load();
                while (active > maximum &&
                       !platform.maximumConcurrentCalls
                            .compare_exchange_weak(maximum, active))
                {
                }
            }

            ~ActiveCall()
            {
                platform.activeCalls.fetch_sub(1);
            }

        private:
            FakeWindowPlatform& platform;
        };

        template<class TMutation>
        Internal::WindowPlatformMutationResult Mutate(
            WindowId id,
            TMutation mutation)
        {
            RecordPlatformCall();
            if (auto failure = TakeFailure())
            {
                return std::unexpected(std::move(*failure));
            }

            WindowConfiguration configuration;
            WindowOperationStatus status;
            {
                std::scoped_lock lock(mutex);
                const auto found = windows.find(id);
                if (found == windows.end())
                {
                    return std::unexpected(DestroyedWindowError());
                }
                status = mutation(found->second);
                configuration = found->second;
            }

            std::scoped_lock sinkLock(sinkMutex);
            if (sink)
            {
                sink->OnPlatformConfigurationChanged(
                    id,
                    configuration);
            }
            return Internal::WindowPlatformMutation {
                .status = status,
                .configuration = std::move(configuration)
            };
        }

        void RecordPlatformCall()
        {
            ActiveCall active(*this);
            {
                std::scoped_lock lock(mutex);
                platformCallThreads.emplace_back(
                    std::this_thread::get_id());
            }
            if (callDelay.count() > 0)
            {
                std::this_thread::sleep_for(callDelay);
            }
        }

        std::optional<WindowError> TakeFailure()
        {
            std::scoped_lock lock(mutex);
            auto result = std::move(nextFailure);
            nextFailure.reset();
            return result;
        }

        void Deliver(QueuedEvent event) noexcept
        {
            {
                std::scoped_lock lock(mutex);
                if (event.type == EventType::Configuration)
                {
                    const auto found = windows.find(event.id);
                    if (found == windows.end())
                    {
                        return;
                    }
                    found->second = event.configuration;
                }
                else if (!windows.contains(event.id))
                {
                    return;
                }
            }
            std::scoped_lock sinkLock(sinkMutex);
            if (!sink)
            {
                return;
            }

            if (event.type == EventType::Configuration)
            {
                sink->OnPlatformConfigurationChanged(
                    event.id,
                    std::move(event.configuration));
                return;
            }

            if (!sink->OnPlatformCloseRequested(
                    event.id,
                    event.closeReason))
            {
                return;
            }

            {
                std::scoped_lock lock(mutex);
                windows.erase(event.id);
            }
            sink->OnPlatformClosed(
                event.id,
                event.closeReason);
        }

        void QueueEvent(QueuedEvent event)
        {
            std::vector<QueuedEvent> events;
            events.emplace_back(std::move(event));
            QueueEvents(std::move(events));
        }

        void QueueEvents(std::vector<QueuedEvent> events)
        {
            std::weak_ptr<std::atomic<bool>> weakAlive = alive;
            pump.Queue(
                [
                    this,
                    weakAlive,
                    events = std::move(events)
                ]() mutable
                {
                    const auto current = weakAlive.lock();
                    if (!current || !current->load())
                    {
                        return;
                    }
                    for (auto& event : events)
                    {
                        Deliver(std::move(event));
                    }
                });
        }

        static FramebufferSize ToFramebuffer(
            LogicalSize size,
            WindowScale scale) noexcept
        {
            return FramebufferSize {
                .width = static_cast<std::uint32_t>(
                    std::lround(size.width * scale.x)),
                .height = static_cast<std::uint32_t>(
                    std::lround(size.height * scale.y))
            };
        }

        static WindowError DestroyedWindowError()
        {
            return WindowError {
                .code = WindowErrorCode::WindowDestroyed,
                .message = "The fake native window does not exist."
            };
        }

        static WindowOperationResult DestroyedError()
        {
            return std::unexpected(DestroyedWindowError());
        }

        static WindowError UnsupportedError()
        {
            return WindowError {
                .code = WindowErrorCode::Unsupported,
                .message = "The fake platform does not support this operation."
            };
        }

        mutable std::mutex mutex;
        mutable std::recursive_mutex sinkMutex;
        FakeDesktopEventPump& pump;
        std::shared_ptr<std::atomic<bool>> alive {
            std::make_shared<std::atomic<bool>>(true)
        };
        Internal::IWindowPlatformEventSink* sink { nullptr };
        std::unordered_map<WindowId, WindowConfiguration> windows;
        std::optional<WindowError> nextFailure;
        std::vector<std::thread::id> platformCallThreads;
        std::chrono::milliseconds callDelay { 0 };
        std::atomic<std::size_t> activeCalls { 0 };
        std::atomic<std::size_t> maximumConcurrentCalls { 0 };
        std::shared_ptr<std::atomic<std::size_t>> destructionCount;
        std::shared_ptr<FakeWindowPlatformLifecycle> lifecycle;
    };
}
