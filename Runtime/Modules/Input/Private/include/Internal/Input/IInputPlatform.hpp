/**
 * @file IInputPlatform.hpp
 * @brief Private platform SPI consumed by the Input facade.
 */

#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "TGE/Input/InputDevice.hpp"
#include "TGE/Input/InputError.hpp"
#include "TGE/Input/InputEvents.hpp"
#include "TGE/Input/InputTypes.hpp"

namespace TGE::Internal
{
    /**
     * @brief Opaque engine-owned routing target understood by desktop adapters.
     *
     * Values are issued by the shared desktop integration and are never native
     * window handles. An invalid target represents an unbound/global context.
     */
    class InputPlatformTarget final
    {
    public:
        constexpr InputPlatformTarget() noexcept = default;

        [[nodiscard]] static constexpr InputPlatformTarget FromValue(
            std::uint64_t value) noexcept
        {
            return InputPlatformTarget(value);
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0;
        }

        auto operator<=>(const InputPlatformTarget&) const = default;

    private:
        explicit constexpr InputPlatformTarget(
            std::uint64_t value) noexcept
            : value(value)
        {
        }

        std::uint64_t value { 0 };
    };

    struct InputPlatformContextState
    {
        InputContextConfiguration configuration;
        InputContextCapabilities capabilities;
    };

    struct InputPlatformMutation
    {
        InputOperationStatus status { InputOperationStatus::Applied };
        InputContextConfiguration configuration;
    };

    using InputPlatformCreateResult =
        std::expected<InputPlatformContextState, InputError>;
    using InputPlatformMutationResult =
        std::expected<InputPlatformMutation, InputError>;

    struct InputPlatformKeyboardEvent
    {
        InputContextId context;
        InputDeviceId device;
        PhysicalKeyCode physicalCode;
        std::string logicalName;
        InputAction action { InputAction::Pressed };
        InputModifiers modifiers;
        bool repeat { false };
    };

    struct InputPlatformTextEvent
    {
        InputContextId context;
        InputDeviceId device;
        std::string text;
    };

    struct InputPlatformPointerMovedEvent
    {
        InputContextId context;
        InputDeviceId device;
        InputPoint position {};
        InputDelta delta {};
        bool relative { false };
    };

    struct InputPlatformPointerButtonEvent
    {
        InputContextId context;
        InputDeviceId device;
        PointerButton button { PointerButton::Primary };
        InputAction action { InputAction::Pressed };
        InputPoint position {};
    };

    struct InputPlatformPointerWheelEvent
    {
        InputContextId context;
        InputDeviceId device;
        InputDelta delta {};
        InputWheelUnit unit { InputWheelUnit::Lines };
    };

    struct InputPlatformTouchEvent
    {
        InputContextId context;
        InputDeviceId device;
        std::uint64_t contact { 0 };
        TouchAction action { TouchAction::Began };
        InputPoint position {};
        float pressure { 0.0F };
    };

    /**
     * @brief Receives translated platform input on the desktop event runtime.
     *
     * Implementations may defensively accept callbacks from another thread;
     * InputManager serializes them before publishing public events.
     */
    class IInputPlatformEventSink
    {
    public:
        virtual ~IInputPlatformEventSink() = default;

        virtual void OnKeyboard(
            InputPlatformKeyboardEvent event) noexcept = 0;
        virtual void OnText(
            InputPlatformTextEvent event) noexcept = 0;
        virtual void OnPointerMoved(
            InputPlatformPointerMovedEvent event) noexcept = 0;
        virtual void OnPointerButton(
            InputPlatformPointerButtonEvent event) noexcept = 0;
        virtual void OnPointerWheel(
            InputPlatformPointerWheelEvent event) noexcept = 0;
        virtual void OnTouch(
            InputPlatformTouchEvent event) noexcept = 0;
        /**
         * @brief Report effective changes caused by a routing target.
         *
         * Backends call this when target activation changes capture or
         * relative mode without a caller mutation. When inputInvalidated is
         * true, the facade also clears keys, buttons, modifiers, and touches
         * whose balancing release events may no longer reach the context.
         * No synthetic public input events are generated.
         */
        virtual void OnTargetInputChanged(
            InputContextId context,
            InputContextConfiguration configuration,
            bool inputInvalidated) noexcept = 0;
        virtual void OnDeviceChanged(
            InputDeviceChangeKind change,
            InputDeviceDescriptor device) noexcept = 0;
    };

    /**
     * @brief Synchronous mechanism interface invoked on DesktopEventRuntime.
     *
     * The platform translates operating-system input but does not define
     * context ownership, public event ordering, state snapshots, or mutation
     * semantics. No SDL or native type appears in this contract.
     *
     * Every method except SetEventSink is invoked serially on the desktop event
     * runtime thread. SetEventSink is a thread-safe lifecycle boundary: after
     * SetEventSink(nullptr) returns, the previous sink receives no new
     * callbacks and all callbacks that had already entered it have returned.
     */
    class IInputPlatform
    {
    public:
        virtual ~IInputPlatform() = default;

        virtual void SetEventSink(
            IInputPlatformEventSink* sink) noexcept = 0;

        /**
         * @brief Release thread-affine adapter state after all contexts close.
         *
         * The facade invokes this at most once on the desktop event thread.
         * The C++ adapter may be destructed later on an arbitrary thread and
         * therefore must not defer thread-affine cleanup to its destructor.
         */
        virtual void Shutdown() noexcept = 0;

        [[nodiscard]] virtual std::vector<InputDeviceDescriptor>
            ConnectedDevices() const = 0;

        [[nodiscard]] virtual InputPlatformCreateResult CreateContext(
            InputContextId id,
            const InputContextDescriptor& descriptor,
            InputPlatformTarget target) = 0;

        [[nodiscard]] virtual InputOperationResult DestroyContext(
            InputContextId id) = 0;

        [[nodiscard]] virtual InputPlatformMutationResult SetEnabled(
            InputContextId id,
            bool enabled) = 0;
        [[nodiscard]] virtual InputPlatformMutationResult SetCapture(
            InputContextId id,
            bool captured) = 0;
        [[nodiscard]] virtual InputPlatformMutationResult
            SetRelativePointerMode(
                InputContextId id,
                bool enabled) = 0;
    };
}
