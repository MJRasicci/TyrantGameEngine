#pragma once

#include <memory>

#include "Internal/Desktop/IDesktopEventPump.hpp"
#include "Internal/Graphics/IWindowPresentationTargetProvider.hpp"
#include "Internal/Graphics/IWindowPlatform.hpp"
#include "Internal/Input/IInputPlatform.hpp"

namespace TGE::Internal
{
    /**
     * @brief Private adapters backed by one SDL desktop event queue.
     *
     * The components share backend state, but expose only Tyrant's private
     * platform contracts. The event pump must outlive and drive both platform
     * adapters.
     */
    struct SDLDesktopComponents
    {
        std::unique_ptr<IDesktopEventPump> eventPump;
        std::unique_ptr<IWindowPlatform> windowPlatform;
        std::unique_ptr<IWindowPresentationTargetProvider>
            presentationTargetProvider;
        std::unique_ptr<IInputPlatform> inputPlatform;
    };

    /**
     * @brief Create the private SDL desktop integration island.
     *
     * SDL initialization remains deferred until IDesktopEventPump::Start so
     * all SDL video and event work stays on DesktopEventRuntime's event thread.
     */
    [[nodiscard]] SDLDesktopComponents CreateSDLDesktopComponents();
}
