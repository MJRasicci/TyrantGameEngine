#pragma once

#include <optional>
#include <string>

#include "TGE/Graphics/WindowId.hpp"
#include "TGE/Graphics/WindowTypes.hpp"

namespace TGE
{
    /**
     * @brief Effective, queryable configuration of a live window.
     *
     * This is the configuration the platform actually applied, which may be a
     * best-effort normalization of the requested WindowDescriptor.
     */
    struct WindowConfiguration
    {
        std::string title;
        WindowRole role { WindowRole::TopLevel };
        std::optional<WindowId> parent;
        WindowGeometry geometry {};
        WindowState state { WindowState::Normal };
        WindowChrome chrome {};
        WindowModality modality { WindowModality::Modeless };
        bool visible { true };
        bool alwaysOnTop { false };
        bool inputEnabled { true };
        bool focused { false };

        bool operator==(const WindowConfiguration&) const = default;
    };

    /**
     * @brief Features a concrete platform can honor for one window.
     *
     * Capabilities are a defensive snapshot, not an instruction to branch on
     * every operation. Creation remains best-effort and mutations report
     * unsupported or normalized outcomes without invalidating the window.
     */
    struct WindowCapabilities
    {
        bool decorations { false };
        bool resizing { false };
        bool minimizing { false };
        bool maximizing { false };
        bool fullscreen { false };
        bool positioning { false };
        bool visibility { false };
        bool focus { false };
        bool inputControl { false };
        bool alwaysOnTop { false };
        bool parentWindows { false };
        bool childWindows { false };
        bool toolWindows { false };
        bool dialogWindows { false };
        bool independentAxisScale { false };
        bool applicationModality { false };

        bool operator==(const WindowCapabilities&) const = default;
    };
}
