/**
 * @file IInputContext.hpp
 * @brief Public control, state, and event surface for one input context.
 */

#pragma once

#include <memory>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Input/InputContextId.hpp"
#include "TGE/Input/InputError.hpp"
#include "TGE/Input/InputEvents.hpp"
#include "TGE/Input/InputStateSnapshot.hpp"
#include "TGE/Input/InputTypes.hpp"

namespace TGE
{
    /**
     * @brief Thread-safe platform-neutral facade for one routed input context.
     *
     * Event callbacks are serialized in manager-wide event order. No manager
     * or platform lock is held while application callbacks execute, so a
     * callback may safely inspect state, reset subscriptions, or initiate an
     * asynchronous mutation.
     */
    class TGE_API IInputContext
    {
    public:
        virtual ~IInputContext();

        [[nodiscard]] virtual InputContextId Id() const noexcept = 0;
        [[nodiscard]] virtual InputContextDescriptor RequestedDescriptor()
            const = 0;
        [[nodiscard]] virtual InputContextConfiguration
            EffectiveConfiguration() const = 0;
        [[nodiscard]] virtual InputContextCapabilities Capabilities()
            const noexcept = 0;
        [[nodiscard]] virtual InputContextLifecycleState LifecycleState()
            const noexcept = 0;
        [[nodiscard]] virtual std::shared_ptr<const InputStateSnapshot>
            StateSnapshot() const = 0;

        /**
         * @brief Enable or suppress routing without blocking an event loop.
         *
         * Disabling a context atomically clears pressed keys, buttons, and
         * touches from its next state snapshot. Suppressed platform events are
         * neither published nor applied to state.
         */
        virtual Task<InputOperationResult> SetEnabledAsync(
            bool enabled) = 0;
        virtual Task<InputOperationResult> SetCaptureAsync(
            bool captured) = 0;
        virtual Task<InputOperationResult> SetRelativePointerModeAsync(
            bool enabled) = 0;

        [[nodiscard]] virtual InputSubscription SubscribeKeyboard(
            KeyboardInputCallback callback) = 0;
        [[nodiscard]] virtual InputSubscription SubscribeText(
            TextInputCallback callback) = 0;
        [[nodiscard]] virtual InputSubscription SubscribePointerMoved(
            PointerMovedCallback callback) = 0;
        [[nodiscard]] virtual InputSubscription SubscribePointerButton(
            PointerButtonCallback callback) = 0;
        [[nodiscard]] virtual InputSubscription SubscribePointerWheel(
            PointerWheelCallback callback) = 0;
        [[nodiscard]] virtual InputSubscription SubscribeTouch(
            TouchInputCallback callback) = 0;
    };
}
