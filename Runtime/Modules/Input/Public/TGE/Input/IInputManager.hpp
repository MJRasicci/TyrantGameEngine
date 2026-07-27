/**
 * @file IInputManager.hpp
 * @brief DI-registerable owner and directory of input contexts and devices.
 */

#pragma once

#include <expected>
#include <memory>
#include <vector>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Input/IInputContext.hpp"
#include "TGE/Input/InputDevice.hpp"

namespace TGE
{
    using InputContextResult =
        std::expected<std::shared_ptr<IInputContext>, InputError>;

    /**
     * @brief Thread-safe owner of application input contexts and device state.
     *
     * Context and device accessors return owned snapshots. Event callbacks are
     * serialized with context events under one monotonically increasing
     * manager-wide sequence.
     */
    class TGE_API IInputManager
    {
    public:
        virtual ~IInputManager();

        [[nodiscard]] virtual Task<InputContextResult> CreateContextAsync(
            InputContextDescriptor descriptor) = 0;

        [[nodiscard]] virtual std::shared_ptr<IInputContext> FindContext(
            InputContextId id) const noexcept = 0;

        /**
         * @brief Snapshot of owned contexts in creation order.
         */
        [[nodiscard]] virtual std::vector<std::shared_ptr<IInputContext>>
            Contexts() const = 0;

        virtual Task<InputOperationResult> DestroyContextAsync(
            InputContextId id) = 0;

        /**
         * @brief Snapshot of currently connected devices in discovery order.
         */
        [[nodiscard]] virtual std::vector<InputDeviceDescriptor>
            Devices() const = 0;

        [[nodiscard]] virtual InputSubscription SubscribeDeviceChanged(
            InputDeviceChangedCallback callback) = 0;
    };
}
