#include "SDLDesktopState.hpp"

#include <utility>

namespace TGE::Internal
{
    namespace
    {
        class SDLWindowPlatform final : public IWindowPlatform
        {
        public:
            explicit SDLWindowPlatform(
                std::shared_ptr<SDLDesktopState> state)
                : state(std::move(state))
            {
            }

            ~SDLWindowPlatform() override
            {
                state->SetWindowEventSink(nullptr);
            }

            void SetEventSink(
                IWindowPlatformEventSink* sink) noexcept override
            {
                state->SetWindowEventSink(sink);
            }

            void Shutdown() noexcept override
            {
                state->ShutdownWindows();
            }

            WindowPlatformCreateResult CreateWindow(
                WindowId id,
                const WindowDescriptor& descriptor) override
            {
                return state->CreateWindow(id, descriptor);
            }

            WindowOperationResult DestroyWindow(
                WindowId id) override
            {
                return state->DestroyWindow(id);
            }

            WindowPlatformMutationResult SetTitle(
                WindowId id,
                std::string title) override
            {
                return state->SetTitle(id, std::move(title));
            }

            WindowPlatformMutationResult SetLogicalBounds(
                WindowId id,
                LogicalBounds bounds) override
            {
                return state->SetLogicalBounds(id, bounds);
            }

            WindowPlatformMutationResult SetState(
                WindowId id,
                WindowState windowState) override
            {
                return state->SetState(id, windowState);
            }

            WindowPlatformMutationResult SetVisible(
                WindowId id,
                bool visible) override
            {
                return state->SetVisible(id, visible);
            }

            WindowPlatformMutationResult RequestFocus(
                WindowId id) override
            {
                return state->RequestFocus(id);
            }

            WindowPlatformMutationResult SetInputEnabled(
                WindowId id,
                bool enabled) override
            {
                return state->SetInputEnabled(id, enabled);
            }

            WindowOperationResult RequestClose(
                WindowId id) override
            {
                return state->RequestClose(id);
            }

        private:
            std::shared_ptr<SDLDesktopState> state;
        };
    }

    std::unique_ptr<IWindowPlatform> CreateSDLWindowPlatform(
        std::shared_ptr<SDLDesktopState> state)
    {
        return std::make_unique<SDLWindowPlatform>(
            std::move(state));
    }
}
