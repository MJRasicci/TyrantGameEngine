#pragma once

#include <optional>
#include <string>

#include "TGE/Graphics/WindowId.hpp"
#include "TGE/Graphics/WindowTypes.hpp"

namespace TGE
{
    /**
     * @brief Platform-neutral description used to create a window.
     *
     * parent identifies ownership for tools and dialogs, and containment for
     * child windows. A backend may reject unsupported combinations explicitly.
     */
    struct WindowDescriptor
    {
        std::string title;
        WindowRole role { WindowRole::TopLevel };
        std::optional<WindowId> parent;
        WindowBounds bounds {};
        WindowState state { WindowState::Normal };
        WindowChrome chrome {};
        bool initiallyVisible { true };
        bool alwaysOnTop { false };
        bool acceptsInput { true };
    };
}
