#pragma once

#include <memory>

#include "TGE/Export.hpp"
#include "TGE/Graphics/IWindowManager.hpp"

namespace TGE::Internal
{
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
        explicit WindowManager(std::unique_ptr<IWindowPlatform> platform);
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

    private:
        struct State;
        std::shared_ptr<State> state;
    };
}
