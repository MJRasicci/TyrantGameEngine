#pragma once

#include <cstdint>

namespace TGE
{
    /**
     * @brief Position expressed in platform-independent logical units.
     */
    struct LogicalPoint
    {
        float x { 0.0F };
        float y { 0.0F };

        bool operator==(const LogicalPoint&) const = default;
    };

    /**
     * @brief Size expressed in platform-independent logical units.
     */
    struct LogicalSize
    {
        float width { 1280.0F };
        float height { 720.0F };

        bool operator==(const LogicalSize&) const = default;
    };

    struct LogicalBounds
    {
        LogicalPoint position {};
        LogicalSize size {};

        bool operator==(const LogicalBounds&) const = default;
    };

    /**
     * @brief Drawable dimensions expressed in physical pixels.
     */
    struct FramebufferSize
    {
        std::uint32_t width { 1280 };
        std::uint32_t height { 720 };

        bool operator==(const FramebufferSize&) const = default;
    };

    /**
     * @brief Physical-pixel scale applied to one logical unit.
     */
    struct WindowScale
    {
        float x { 1.0F };
        float y { 1.0F };

        bool operator==(const WindowScale&) const = default;
    };

    /**
     * @brief Coherent layout and rendering geometry snapshot.
     *
     * Backends update all three values together during monitor-scale and
     * resize transitions. Layout consumes logicalBounds while renderers consume
     * framebufferSize.
     */
    struct WindowGeometry
    {
        LogicalBounds logicalBounds {};
        FramebufferSize framebufferSize {};
        WindowScale scale {};

        bool operator==(const WindowGeometry&) const = default;
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

    /**
     * @brief Input suppression policy established when a window is created.
     *
     * Modality never starts a nested event loop. The window manager maintains
     * asynchronous input suppression until the modal window is destroyed.
     */
    enum class WindowModality
    {
        Modeless,
        DisableParent,
        DisableParentTree,
        ApplicationModal
    };

    enum class WindowState
    {
        Normal,
        Minimized,
        Maximized,
        Fullscreen
    };

    enum class WindowLifecycleState
    {
        Open,
        Destroying,
        Destroyed
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

        bool operator==(const WindowChrome&) const = default;
    };

    // Transitional aliases for the initial Graphics scaffold. New APIs use the
    // explicit logical-unit names above.
    using WindowPoint = LogicalPoint;
    using WindowSize = LogicalSize;
    using WindowBounds = LogicalBounds;
}
