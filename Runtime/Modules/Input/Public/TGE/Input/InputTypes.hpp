/**
 * @file InputTypes.hpp
 * @brief Context configuration and portable input value types.
 */

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>

namespace TGE
{
    /**
     * @brief Context-local logical coordinate, independent of framebuffer DPI.
     *
     * A context bound to a desktop window uses the same logical coordinate
     * space as that window's content area. Unbound contexts may leave the
     * value at its default when a position is not meaningful.
     */
    struct InputPoint
    {
        double x { 0.0 };
        double y { 0.0 };

        bool operator==(const InputPoint&) const = default;
    };

    /**
     * @brief Delta in logical units unless an event explicitly says otherwise.
     */
    struct InputDelta
    {
        double x { 0.0 };
        double y { 0.0 };

        bool operator==(const InputDelta&) const = default;
    };

    /**
     * @brief Portable physical key location assigned by the Input subsystem.
     *
     * Values identify physical key positions after backend translation and are
     * never raw platform scan codes.
     */
    class PhysicalKeyCode final
    {
    public:
        constexpr PhysicalKeyCode() noexcept = default;

        [[nodiscard]] static constexpr PhysicalKeyCode FromValue(
            std::uint32_t value) noexcept
        {
            return PhysicalKeyCode(value);
        }

        [[nodiscard]] constexpr std::uint32_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0;
        }

        auto operator<=>(const PhysicalKeyCode&) const = default;

    private:
        explicit constexpr PhysicalKeyCode(std::uint32_t value) noexcept
            : value(value)
        {
        }

        std::uint32_t value { 0 };
    };

    /**
     * @brief Modifier state accompanying a keyboard event.
     */
    struct InputModifiers
    {
        bool shift { false };
        bool control { false };
        bool alt { false };
        bool super { false };
        bool capsLock { false };
        bool numLock { false };

        bool operator==(const InputModifiers&) const = default;
    };

    enum class InputAction
    {
        Pressed,
        Released
    };

    enum class PointerButton
    {
        Primary,
        Secondary,
        Middle,
        Auxiliary1,
        Auxiliary2,
        Other
    };

    enum class InputWheelUnit
    {
        Lines,
        Pixels
    };

    enum class TouchAction
    {
        Began,
        Moved,
        Ended,
        Cancelled
    };

    enum class InputContextLifecycleState
    {
        Open,
        Destroying,
        Destroyed
    };

    /**
     * @brief Caller preferences used to create an input context.
     */
    struct InputContextDescriptor
    {
        /**
         * @brief Diagnostic name; it need not be unique.
         */
        std::string name;

        /**
         * @brief Whether routed events should initially update state or publish.
         */
        bool initiallyEnabled { true };

        /**
         * @brief Request pointer capture when supported by the target.
         */
        bool requestCapture { false };

        /**
         * @brief Request relative pointer motion when supported by the target.
         */
        bool requestRelativePointerMode { false };

        bool operator==(const InputContextDescriptor&) const = default;
    };

    /**
     * @brief Effective, queryable configuration applied to an input context.
     */
    struct InputContextConfiguration
    {
        std::string name;
        bool enabled { true };
        bool captured { false };
        bool relativePointerMode { false };

        bool operator==(const InputContextConfiguration&) const = default;
    };

    /**
     * @brief Features the selected platform can honor for an input context.
     */
    struct InputContextCapabilities
    {
        bool enableControl { false };
        bool capture { false };
        bool relativePointerMode { false };
        bool keyboard { false };
        bool text { false };
        bool pointer { false };
        bool touch { false };

        bool operator==(const InputContextCapabilities&) const = default;
    };
}

template<>
struct std::hash<TGE::PhysicalKeyCode>
{
    std::size_t operator()(
        TGE::PhysicalKeyCode code) const noexcept
    {
        return std::hash<std::uint32_t> {}(code.Value());
    }
};
