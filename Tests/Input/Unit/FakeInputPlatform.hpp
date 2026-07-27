#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Internal/Input/IInputPlatform.hpp"

namespace TGE::Tests
{
    struct FakeInputPlatformState final
    {
        struct Call
        {
            std::string operation;
            InputContextId context;
            std::thread::id thread;
        };

        mutable std::mutex mutex;
        std::vector<Call> calls;
        std::vector<InputContextId> destroyed;
        std::size_t activeCalls { 0 };
        std::size_t maximumConcurrentCalls { 0 };
        std::size_t shutdownCount { 0 };
        std::size_t destructionCount { 0 };
        std::chrono::milliseconds callDelay { 0 };
    };

    class FakeInputPlatform final
        : public Internal::IInputPlatform
    {
    private:
        struct ContextRecord
        {
            InputContextConfiguration configuration;
            InputContextCapabilities capabilities;
            Internal::InputPlatformTarget target;
        };

        class CallScope final
        {
        public:
            CallScope(
                std::shared_ptr<FakeInputPlatformState> state,
                std::string operation,
                InputContextId context = {})
                : state(std::move(state))
            {
                std::chrono::milliseconds delay;
                {
                    std::scoped_lock lock(this->state->mutex);
                    ++this->state->activeCalls;
                    this->state->maximumConcurrentCalls = std::max(
                        this->state->maximumConcurrentCalls,
                        this->state->activeCalls);
                    this->state->calls.emplace_back(
                        FakeInputPlatformState::Call {
                            .operation = std::move(operation),
                            .context = context,
                            .thread = std::this_thread::get_id()
                        });
                    delay = this->state->callDelay;
                }
                if (delay.count() > 0)
                {
                    std::this_thread::sleep_for(delay);
                }
            }

            ~CallScope()
            {
                std::scoped_lock lock(state->mutex);
                --state->activeCalls;
            }

        private:
            std::shared_ptr<FakeInputPlatformState> state;
        };

    public:
        explicit FakeInputPlatform(
            std::shared_ptr<FakeInputPlatformState> state =
                std::make_shared<FakeInputPlatformState>())
            : state(std::move(state))
        {
            capabilities = InputContextCapabilities {
                .enableControl = true,
                .capture = true,
                .relativePointerMode = true,
                .keyboard = true,
                .text = true,
                .pointer = true,
                .touch = true
            };
        }

        ~FakeInputPlatform() override
        {
            std::scoped_lock lock(state->mutex);
            ++state->destructionCount;
        }

        void SetEventSink(
            Internal::IInputPlatformEventSink* eventSink) noexcept override
        {
            std::unique_lock lock(sinkMutex);
            sink = eventSink;
            if (!eventSink)
            {
                sinkChanged.wait(
                    lock,
                    [this]
                    {
                        return callbacksInFlight == 0;
                    });
            }
        }

        void Shutdown() noexcept override
        {
            CallScope call(state, "Shutdown");
            std::scoped_lock lock(state->mutex);
            ++state->shutdownCount;
        }

        std::vector<InputDeviceDescriptor>
            ConnectedDevices() const override
        {
            std::scoped_lock lock(platformMutex);
            return connectedDevices;
        }

        Internal::InputPlatformCreateResult CreateContext(
            InputContextId id,
            const InputContextDescriptor& descriptor,
            Internal::InputPlatformTarget target) override
        {
            CallScope call(state, "CreateContext", id);
            if (const auto failure = TakeFailure())
            {
                return std::unexpected(*failure);
            }
            if (throwNext.exchange(false))
            {
                throw std::runtime_error("fake input platform exception");
            }

            auto configuration = InputContextConfiguration {
                .name = descriptor.name,
                .enabled = descriptor.initiallyEnabled,
                .captured =
                    descriptor.requestCapture && capabilities.capture,
                .relativePointerMode =
                    descriptor.requestRelativePointerMode &&
                    capabilities.relativePointerMode
            };
            {
                std::scoped_lock lock(platformMutex);
                contexts.emplace(
                    id,
                    ContextRecord {
                        .configuration = configuration,
                        .capabilities = capabilities,
                        .target = target
                    });
            }
            return Internal::InputPlatformContextState {
                .configuration = std::move(configuration),
                .capabilities = capabilities
            };
        }

        InputOperationResult DestroyContext(
            InputContextId id) override
        {
            CallScope call(state, "DestroyContext", id);
            if (const auto failure = TakeFailure())
            {
                return std::unexpected(*failure);
            }

            {
                std::scoped_lock lock(platformMutex);
                if (contexts.erase(id) == 0)
                {
                    return std::unexpected(NotFoundError());
                }
            }
            {
                std::scoped_lock lock(state->mutex);
                state->destroyed.emplace_back(id);
            }
            return InputOperationStatus::Applied;
        }

        Internal::InputPlatformMutationResult SetEnabled(
            InputContextId id,
            bool enabled) override
        {
            if (!capabilities.enableControl)
            {
                CallScope call(state, "SetEnabled", id);
                return std::unexpected(UnsupportedError());
            }
            return Mutate(
                id,
                "SetEnabled",
                [enabled](InputContextConfiguration& configuration)
                {
                    configuration.enabled = enabled;
                });
        }

        Internal::InputPlatformMutationResult SetCapture(
            InputContextId id,
            bool captured) override
        {
            if (!capabilities.capture)
            {
                CallScope call(state, "SetCapture", id);
                return std::unexpected(UnsupportedError());
            }
            return Mutate(
                id,
                "SetCapture",
                [captured](InputContextConfiguration& configuration)
                {
                    configuration.captured = captured;
                });
        }

        Internal::InputPlatformMutationResult SetRelativePointerMode(
            InputContextId id,
            bool enabled) override
        {
            if (!capabilities.relativePointerMode)
            {
                CallScope call(state, "SetRelativePointerMode", id);
                return std::unexpected(UnsupportedError());
            }
            return Mutate(
                id,
                "SetRelativePointerMode",
                [enabled](InputContextConfiguration& configuration)
                {
                    configuration.relativePointerMode = enabled;
                });
        }

        void EmitKeyboard(
            Internal::InputPlatformKeyboardEvent event)
        {
            Deliver(
                [event = std::move(event)](
                    Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnKeyboard(std::move(event));
                });
        }

        void EmitText(Internal::InputPlatformTextEvent event)
        {
            Deliver(
                [event = std::move(event)](
                    Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnText(std::move(event));
                });
        }

        void EmitPointerMoved(
            Internal::InputPlatformPointerMovedEvent event)
        {
            Deliver(
                [event = std::move(event)](
                    Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnPointerMoved(std::move(event));
                });
        }

        void EmitPointerButton(
            Internal::InputPlatformPointerButtonEvent event)
        {
            Deliver(
                [event = std::move(event)](
                    Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnPointerButton(std::move(event));
                });
        }

        void EmitPointerWheel(
            Internal::InputPlatformPointerWheelEvent event)
        {
            Deliver(
                [event = std::move(event)](
                    Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnPointerWheel(std::move(event));
                });
        }

        void EmitTouch(Internal::InputPlatformTouchEvent event)
        {
            Deliver(
                [event = std::move(event)](
                    Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnTouch(std::move(event));
                });
        }

        void EmitTargetInputChanged(
            InputContextId context,
            InputContextConfiguration configuration,
            bool inputInvalidated)
        {
            Deliver(
                [
                    context,
                    configuration = std::move(configuration),
                    inputInvalidated
                ](Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnTargetInputChanged(
                        context,
                        std::move(configuration),
                        inputInvalidated);
                });
        }

        void EmitDeviceChanged(
            InputDeviceChangeKind change,
            InputDeviceDescriptor device)
        {
            Deliver(
                [change, device = std::move(device)](
                    Internal::IInputPlatformEventSink& target) mutable
                {
                    target.OnDeviceChanged(
                        change,
                        std::move(device));
                });
        }

        [[nodiscard]] std::optional<Internal::InputPlatformTarget>
            TargetFor(InputContextId id) const
        {
            std::scoped_lock lock(platformMutex);
            const auto found = contexts.find(id);
            if (found == contexts.end())
            {
                return {};
            }
            return found->second.target;
        }

        void FailNext(InputError error)
        {
            std::scoped_lock lock(platformMutex);
            nextFailure = std::move(error);
        }

        void ThrowNext()
        {
            throwNext.store(true);
        }

        InputContextCapabilities capabilities;
        std::vector<InputDeviceDescriptor> connectedDevices;

    private:
        template<class TMutation>
        Internal::InputPlatformMutationResult Mutate(
            InputContextId id,
            std::string operation,
            TMutation mutation)
        {
            CallScope call(state, std::move(operation), id);
            if (const auto failure = TakeFailure())
            {
                return std::unexpected(*failure);
            }
            if (throwNext.exchange(false))
            {
                throw std::runtime_error("fake input platform exception");
            }

            std::scoped_lock lock(platformMutex);
            const auto found = contexts.find(id);
            if (found == contexts.end())
            {
                return std::unexpected(NotFoundError());
            }
            mutation(found->second.configuration);
            return Internal::InputPlatformMutation {
                .status = InputOperationStatus::Applied,
                .configuration = found->second.configuration
            };
        }

        template<class TEvent>
        void Deliver(TEvent deliver)
        {
            Internal::IInputPlatformEventSink* target;
            {
                std::scoped_lock lock(sinkMutex);
                target = sink;
                if (!target)
                {
                    return;
                }
                ++callbacksInFlight;
            }

            try
            {
                deliver(*target);
            }
            catch (...)
            {
            }

            {
                std::scoped_lock lock(sinkMutex);
                --callbacksInFlight;
            }
            sinkChanged.notify_all();
        }

        [[nodiscard]] std::optional<InputError> TakeFailure()
        {
            std::scoped_lock lock(platformMutex);
            return std::exchange(nextFailure, {});
        }

        static InputError UnsupportedError()
        {
            return InputError {
                .code = InputErrorCode::Unsupported,
                .message = "The fake platform does not support the operation."
            };
        }

        static InputError NotFoundError()
        {
            return InputError {
                .code = InputErrorCode::ContextNotFound,
                .message = "The fake platform context was not found."
            };
        }

        std::shared_ptr<FakeInputPlatformState> state;
        mutable std::mutex platformMutex;
        std::unordered_map<InputContextId, ContextRecord> contexts;
        std::optional<InputError> nextFailure;
        std::atomic<bool> throwNext { false };

        mutable std::mutex sinkMutex;
        std::condition_variable sinkChanged;
        Internal::IInputPlatformEventSink* sink { nullptr };
        std::size_t callbacksInFlight { 0 };
    };
}
