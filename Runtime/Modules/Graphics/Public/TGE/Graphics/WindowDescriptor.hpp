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
     * child windows. Backends normalize unsupported preferences where a usable
     * window can still be created.
     */
    struct WindowDescriptor
    {
        std::string title;
        WindowRole role { WindowRole::TopLevel };
        std::optional<WindowId> parent;
        LogicalBounds bounds {};
        WindowState state { WindowState::Normal };
        WindowChrome chrome {};
        WindowModality modality { WindowModality::Modeless };
        bool initiallyVisible { true };
        bool alwaysOnTop { false };
        bool acceptsInput { true };

        bool operator==(const WindowDescriptor&) const = default;
    };
}
