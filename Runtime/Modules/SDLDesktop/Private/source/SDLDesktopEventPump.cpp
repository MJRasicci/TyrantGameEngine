#include "SDLDesktopState.hpp"

#include <utility>

namespace TGE::Internal
{
    namespace
    {
        class SDLDesktopEventPump final : public IDesktopEventPump
        {
        public:
            explicit SDLDesktopEventPump(
                std::shared_ptr<SDLDesktopState> state)
                : state(std::move(state))
            {
            }

            DesktopEventResult Start() override
            {
                return state->Start();
            }

            void Wake() noexcept override
            {
                state->Wake();
            }

            void PumpEvents(
                std::chrono::milliseconds maxWait) noexcept override
            {
                state->PumpEvents(maxWait);
            }

            void Stop() noexcept override
            {
                state->Stop();
            }

        private:
            std::shared_ptr<SDLDesktopState> state;
        };
    }

    std::unique_ptr<IDesktopEventPump> CreateSDLDesktopEventPump(
        std::shared_ptr<SDLDesktopState> state)
    {
        return std::make_unique<SDLDesktopEventPump>(
            std::move(state));
    }
}
