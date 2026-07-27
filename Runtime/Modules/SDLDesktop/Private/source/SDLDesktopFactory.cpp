#include "Internal/SDLDesktop/SDLDesktopFactory.hpp"

#include "SDLDesktopState.hpp"

namespace TGE::Internal
{
    SDLDesktopComponents CreateSDLDesktopComponents()
    {
        auto state = std::make_shared<SDLDesktopState>();
        return SDLDesktopComponents {
            .eventPump = CreateSDLDesktopEventPump(state),
            .windowPlatform = CreateSDLWindowPlatform(state),
            .inputPlatform = CreateSDLInputPlatform(std::move(state))
        };
    }
}
