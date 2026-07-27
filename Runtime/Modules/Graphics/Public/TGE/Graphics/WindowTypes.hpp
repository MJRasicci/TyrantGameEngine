#pragma once

#include <compare>
#include <cstdint>

namespace TGE
{
    struct WindowPoint
    {
        std::int32_t x { 0 };
        std::int32_t y { 0 };

        auto operator<=>(const WindowPoint&) const = default;
    };

    struct WindowSize
    {
        std::uint32_t width { 1280 };
        std::uint32_t height { 720 };

        auto operator<=>(const WindowSize&) const = default;
    };

    struct WindowBounds
    {
        WindowPoint position {};
        WindowSize size {};

        auto operator<=>(const WindowBounds&) const = default;
    };

    /**
     * @brief The relationship a window has to the rest of the application.
     */
    enum class WindowRole
    {
        TopLevel,
        Tool,
        Dialog,
        Modal,
        Child
    };

    enum class WindowState
    {
        Normal,
        Minimized,
        Maximized,
        Fullscreen
    };

    /**
     * @brief Platform-neutral window chrome and interaction preferences.
     */
    struct WindowChrome
    {
        bool decorations { true };
        bool resizable { true };
        bool minimizable { true };
        bool maximizable { true };
        bool closable { true };
    };
}
