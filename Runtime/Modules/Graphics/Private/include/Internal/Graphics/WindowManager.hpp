#pragma once

#include <memory>

#include "TGE/Export.hpp"
#include "TGE/Graphics/IWindowManager.hpp"

namespace TGE::Internal
{
    class DesktopEventRuntime;
    class IWindowPlatform;

    /**
     * @brief Thread-safe facade shared by every native window backend.
     *
     * Platform modules construct this service with their private
     * IWindowPlatform implementation, then register it as IWindowManager.
     */
    class TGE_API WindowManager final : public IWindowManager
    {
    public:
        WindowManager(
            std::shared_ptr<DesktopEventRuntime> runtime,
            std::unique_ptr<IWindowPlatform> platform);
        ~WindowManager() override;

        WindowManager(const WindowManager&) = delete;
        WindowManager& operator=(const WindowManager&) = delete;

        [[nodiscard]] Task<WindowResult> CreateWindowAsync(
            WindowDescriptor descriptor) override;
        [[nodiscard]] std::shared_ptr<IWindow> FindWindow(
            WindowId id) const noexcept override;
        [[nodiscard]] std::vector<std::shared_ptr<IWindow>>
            Windows() const override;
        Task<WindowOperationResult> DestroyWindowAsync(
            WindowId id) override;

        /**
         * @brief Explicitly release platform state before the shared pump stops.
         *
         * Desktop composition owns this lifecycle boundary; IWindowManager
         * deliberately remains an ordinary service rather than a hosted
         * service. The operation is idempotent.
         */
        Task<void> ShutdownAsync();

    private:
        struct State;
        std::shared_ptr<State> state;
    };
}
