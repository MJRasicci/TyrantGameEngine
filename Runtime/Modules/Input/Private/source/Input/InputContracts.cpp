/**
 * @file InputContracts.cpp
 * @brief Implements public Input contract ownership and immutable snapshots.
 */

#include "TGE/Input/IInputContext.hpp"
#include "TGE/Input/IInputManager.hpp"
#include "TGE/Input/InputEvents.hpp"
#include "TGE/Input/InputStateSnapshot.hpp"

#include <atomic>
#include <utility>

namespace TGE
{
    struct InputSubscription::State
    {
        explicit State(std::function<void()> unsubscribe)
            : unsubscribe(std::move(unsubscribe))
        {
        }

        std::atomic<bool> active { true };
        std::function<void()> unsubscribe;
    };

    IInputContext::~IInputContext() = default;
    IInputManager::~IInputManager() = default;

    InputSubscription::InputSubscription() noexcept = default;

    InputSubscription::InputSubscription(
        std::function<void()> unsubscribe)
        : state(std::make_shared<State>(std::move(unsubscribe)))
    {
    }

    InputSubscription::~InputSubscription()
    {
        Reset();
    }

    InputSubscription::InputSubscription(
        InputSubscription&& other) noexcept
        : state(std::move(other.state))
    {
    }

    InputSubscription& InputSubscription::operator=(
        InputSubscription&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            state = std::move(other.state);
        }
        return *this;
    }

    void InputSubscription::Reset() noexcept
    {
        auto current = std::exchange(state, {});
        if (!current || !current->active.exchange(false))
        {
            return;
        }

        try
        {
            if (current->unsubscribe)
            {
                current->unsubscribe();
            }
        }
        catch (...)
        {
            // Destruction and explicit reset are best-effort and noexcept.
        }
    }

    InputSubscription::operator bool() const noexcept
    {
        return state && state->active.load();
    }

    InputStateSnapshot::InputStateSnapshot(
        InputContextId context,
        InputEventSequence sequence,
        InputTimestamp timestamp,
        InputContextConfiguration configuration,
        std::vector<PhysicalKeyCode> pressedKeys,
        InputModifiers modifiers,
        InputPoint pointerPosition,
        std::vector<PointerButton> pressedPointerButtons,
        std::vector<TouchContactState> touches)
        : context(context),
          sequence(sequence),
          timestamp(timestamp),
          configuration(std::move(configuration)),
          pressedKeys(std::move(pressedKeys)),
          modifiers(modifiers),
          pointerPosition(pointerPosition),
          pressedPointerButtons(std::move(pressedPointerButtons)),
          touches(std::move(touches))
    {
    }

    InputContextId InputStateSnapshot::Context() const noexcept
    {
        return context;
    }

    InputEventSequence InputStateSnapshot::Sequence() const noexcept
    {
        return sequence;
    }

    InputTimestamp InputStateSnapshot::Timestamp() const noexcept
    {
        return timestamp;
    }

    const InputContextConfiguration&
        InputStateSnapshot::Configuration() const noexcept
    {
        return configuration;
    }

    const std::vector<PhysicalKeyCode>&
        InputStateSnapshot::PressedKeys() const noexcept
    {
        return pressedKeys;
    }

    const InputModifiers&
        InputStateSnapshot::Modifiers() const noexcept
    {
        return modifiers;
    }

    InputPoint InputStateSnapshot::PointerPosition() const noexcept
    {
        return pointerPosition;
    }

    const std::vector<PointerButton>&
        InputStateSnapshot::PressedPointerButtons() const noexcept
    {
        return pressedPointerButtons;
    }

    const std::vector<TouchContactState>&
        InputStateSnapshot::Touches() const noexcept
    {
        return touches;
    }
}
