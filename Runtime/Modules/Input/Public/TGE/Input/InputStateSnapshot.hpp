/**
 * @file InputStateSnapshot.hpp
 * @brief Immutable coherent state snapshot for one input context.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "TGE/Export.hpp"
#include "TGE/Input/InputContextId.hpp"
#include "TGE/Input/InputTypes.hpp"

namespace TGE
{
    using InputEventSequence = std::uint64_t;
    using InputTimestamp = std::chrono::steady_clock::time_point;

    /**
     * @brief Current state of one active touch contact.
     */
    struct TouchContactState
    {
        std::uint64_t contact { 0 };
        InputPoint position {};
        float pressure { 0.0F };

        bool operator==(const TouchContactState&) const = default;
    };

    /**
     * @brief Coherent immutable state observed after a routed input event.
     *
     * Contexts publish snapshots through shared_ptr<const InputStateSnapshot>.
     * A retained snapshot never changes when later events arrive.
     */
    class TGE_API InputStateSnapshot final
    {
    public:
        InputStateSnapshot(
            InputContextId context,
            InputEventSequence sequence,
            InputTimestamp timestamp,
            InputContextConfiguration configuration,
            std::vector<PhysicalKeyCode> pressedKeys,
            InputModifiers modifiers,
            InputPoint pointerPosition,
            std::vector<PointerButton> pressedPointerButtons,
            std::vector<TouchContactState> touches);

        [[nodiscard]] InputContextId Context() const noexcept;
        [[nodiscard]] InputEventSequence Sequence() const noexcept;
        [[nodiscard]] InputTimestamp Timestamp() const noexcept;
        [[nodiscard]] const InputContextConfiguration& Configuration()
            const noexcept;
        [[nodiscard]] const std::vector<PhysicalKeyCode>& PressedKeys()
            const noexcept;
        [[nodiscard]] const InputModifiers& Modifiers() const noexcept;
        [[nodiscard]] InputPoint PointerPosition() const noexcept;
        [[nodiscard]] const std::vector<PointerButton>& PressedPointerButtons()
            const noexcept;
        [[nodiscard]] const std::vector<TouchContactState>& Touches()
            const noexcept;

    private:
        InputContextId context;
        InputEventSequence sequence { 0 };
        InputTimestamp timestamp {};
        InputContextConfiguration configuration;
        std::vector<PhysicalKeyCode> pressedKeys;
        InputModifiers modifiers;
        InputPoint pointerPosition {};
        std::vector<PointerButton> pressedPointerButtons;
        std::vector<TouchContactState> touches;
    };
}
