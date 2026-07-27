/**
 * @file InputEvents.hpp
 * @brief Typed input events and move-only subscription ownership.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "TGE/Export.hpp"
#include "TGE/Input/InputContextId.hpp"
#include "TGE/Input/InputDevice.hpp"
#include "TGE/Input/InputStateSnapshot.hpp"
#include "TGE/Input/InputTypes.hpp"

namespace TGE
{
    /**
     * @brief Ordering and routing metadata common to context input events.
     *
     * Sequence and timestamp values are strictly monotonic across all events
     * published by one input manager.
     */
    struct InputEventMetadata
    {
        InputContextId context;
        InputDeviceId device;
        InputEventSequence sequence { 0 };
        InputTimestamp timestamp {};

        bool operator==(const InputEventMetadata&) const = default;
    };

    struct KeyboardInputEvent
    {
        InputEventMetadata metadata;
        PhysicalKeyCode physicalCode;
        std::string logicalName;
        InputAction action { InputAction::Pressed };
        InputModifiers modifiers;
        bool repeat { false };

        bool operator==(const KeyboardInputEvent&) const = default;
    };

    /**
     * @brief Committed UTF-8 text suitable for insertion by an application.
     *
     * Key events and text events are intentionally separate: keyboard layout,
     * composition, and input methods may produce text that has no one-to-one
     * relationship with physical key presses.
     */
    struct TextInputEvent
    {
        InputEventMetadata metadata;
        std::string text;

        bool operator==(const TextInputEvent&) const = default;
    };

    struct PointerMovedEvent
    {
        InputEventMetadata metadata;
        InputPoint position {};
        InputDelta delta {};
        bool relative { false };

        bool operator==(const PointerMovedEvent&) const = default;
    };

    struct PointerButtonEvent
    {
        InputEventMetadata metadata;
        PointerButton button { PointerButton::Primary };
        InputAction action { InputAction::Pressed };
        InputPoint position {};

        bool operator==(const PointerButtonEvent&) const = default;
    };

    struct PointerWheelEvent
    {
        InputEventMetadata metadata;
        InputDelta delta {};
        InputWheelUnit unit { InputWheelUnit::Lines };

        bool operator==(const PointerWheelEvent&) const = default;
    };

    struct TouchInputEvent
    {
        InputEventMetadata metadata;
        std::uint64_t contact { 0 };
        TouchAction action { TouchAction::Began };
        InputPoint position {};
        float pressure { 0.0F };

        bool operator==(const TouchInputEvent&) const = default;
    };

    enum class InputDeviceChangeKind
    {
        Connected,
        Updated,
        Disconnected
    };

    /**
     * @brief Device hotplug notification in manager-wide event order.
     */
    struct InputDeviceChangedEvent
    {
        InputEventSequence sequence { 0 };
        InputTimestamp timestamp {};
        InputDeviceChangeKind change { InputDeviceChangeKind::Connected };
        InputDeviceDescriptor device;

        bool operator==(const InputDeviceChangedEvent&) const = default;
    };

    /**
     * @brief Move-only RAII token for an input event subscription.
     */
    class TGE_API InputSubscription final
    {
    public:
        InputSubscription() noexcept;
        explicit InputSubscription(std::function<void()> unsubscribe);
        ~InputSubscription();

        InputSubscription(const InputSubscription&) = delete;
        InputSubscription& operator=(const InputSubscription&) = delete;

        InputSubscription(InputSubscription&& other) noexcept;
        InputSubscription& operator=(InputSubscription&& other) noexcept;

        /**
         * @brief Prevent later queued deliveries and release the subscription.
         *
         * Reset does not interrupt a callback that was already executing on
         * another thread. It is idempotent and safe to call from a callback.
         */
        void Reset() noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        struct State;
        std::shared_ptr<State> state;
    };

    using KeyboardInputCallback =
        std::function<void(const KeyboardInputEvent&)>;
    using TextInputCallback =
        std::function<void(const TextInputEvent&)>;
    using PointerMovedCallback =
        std::function<void(const PointerMovedEvent&)>;
    using PointerButtonCallback =
        std::function<void(const PointerButtonEvent&)>;
    using PointerWheelCallback =
        std::function<void(const PointerWheelEvent&)>;
    using TouchInputCallback =
        std::function<void(const TouchInputEvent&)>;
    using InputDeviceChangedCallback =
        std::function<void(const InputDeviceChangedEvent&)>;
}
