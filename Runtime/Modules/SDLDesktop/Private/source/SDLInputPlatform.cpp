#include "SDLDesktopState.hpp"

#include <utility>

namespace TGE::Internal
{
    namespace
    {
        class SDLInputPlatform final : public IInputPlatform
        {
        public:
            explicit SDLInputPlatform(
                std::shared_ptr<SDLDesktopState> state)
                : state(std::move(state))
            {
            }

            ~SDLInputPlatform() override
            {
                state->SetInputEventSink(nullptr);
            }

            void SetEventSink(
                IInputPlatformEventSink* sink) noexcept override
            {
                state->SetInputEventSink(sink);
            }

            std::vector<InputDeviceDescriptor>
                ConnectedDevices() const override
            {
                return state->ConnectedDevices();
            }

            InputPlatformCreateResult CreateContext(
                InputContextId id,
                const InputContextDescriptor& descriptor,
                InputPlatformTarget target) override
            {
                return state->CreateInputContext(
                    id,
                    descriptor,
                    target);
            }

            InputOperationResult DestroyContext(
                InputContextId id) override
            {
                return state->DestroyInputContext(id);
            }

            InputPlatformMutationResult SetEnabled(
                InputContextId id,
                bool enabled) override
            {
                return state->SetContextEnabled(id, enabled);
            }

            InputPlatformMutationResult SetCapture(
                InputContextId id,
                bool captured) override
            {
                return state->SetContextCapture(id, captured);
            }

            InputPlatformMutationResult SetRelativePointerMode(
                InputContextId id,
                bool enabled) override
            {
                return state->SetRelativePointerMode(id, enabled);
            }

            void Shutdown() noexcept override
            {
                state->ShutdownInput();
            }

        private:
            std::shared_ptr<SDLDesktopState> state;
        };
    }

    std::unique_ptr<IInputPlatform> CreateSDLInputPlatform(
        std::shared_ptr<SDLDesktopState> state)
    {
        return std::make_unique<SDLInputPlatform>(
            std::move(state));
    }
}
