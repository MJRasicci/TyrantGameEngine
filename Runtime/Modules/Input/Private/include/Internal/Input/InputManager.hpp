/**
 * @file InputManager.hpp
 * @brief Private thread-safe Input facade shared by desktop platform modules.
 */

#pragma once

#include <memory>

#include "TGE/Export.hpp"
#include "TGE/Input/IInputManager.hpp"

namespace TGE::Internal
{
    class DesktopEventRuntime;
    class IInputPlatform;
    class InputPlatformTarget;

    /**
     * @brief Serialize platform input through a shared desktop event runtime.
     *
     * Platform composition creates this service with the same runtime used by
     * other desktop adapters, then registers it as IInputManager.
     */
    class TGE_API InputManager final : public IInputManager
    {
    public:
        InputManager(
            std::shared_ptr<DesktopEventRuntime> runtime,
            std::unique_ptr<IInputPlatform> platform);
        ~InputManager() override;

        InputManager(const InputManager&) = delete;
        InputManager& operator=(const InputManager&) = delete;

        [[nodiscard]] Task<InputContextResult> CreateContextAsync(
            InputContextDescriptor descriptor) override;

        /**
         * @brief Create a context routed to an internal desktop target.
         *
         * This overload is private runtime SPI for GUI/backend composition and
         * is not installed as part of the public Input API.
         */
        [[nodiscard]] Task<InputContextResult> CreateContextForTargetAsync(
            InputContextDescriptor descriptor,
            InputPlatformTarget target);

        [[nodiscard]] std::shared_ptr<IInputContext> FindContext(
            InputContextId id) const noexcept override;
        [[nodiscard]] std::vector<std::shared_ptr<IInputContext>>
            Contexts() const override;
        Task<InputOperationResult> DestroyContextAsync(
            InputContextId id) override;
        [[nodiscard]] std::vector<InputDeviceDescriptor>
            Devices() const override;
        [[nodiscard]] InputSubscription SubscribeDeviceChanged(
            InputDeviceChangedCallback callback) override;

        /**
         * @brief Explicitly release input state before the desktop pump stops.
         *
         * GUI composition owns this lifecycle boundary. IInputManager remains
         * an ordinary service rather than a hosted service. The operation is
         * idempotent.
         */
        Task<void> ShutdownAsync();

    private:
        struct State;
        std::shared_ptr<State> state;
    };
}
