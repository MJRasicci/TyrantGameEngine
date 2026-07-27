#pragma once

#include <expected>

#include "Internal/Graphics/IWindowPresentationTargetProvider.hpp"
#include "TGE/Graphics/WindowTypes.hpp"

namespace TGE::Internal
{
    using WindowPresentationResult =
        std::expected<void, WindowError>;

    /**
     * @brief Renderer-owned native presentation lifecycle.
     *
     * Every method runs on the desktop event runtime. DetachWindow is called
     * synchronously before the provider invalidates borrowed native handles.
     */
    class IWindowPresenter
    {
    public:
        virtual ~IWindowPresenter() = default;

        [[nodiscard]] virtual WindowPresentationResult AttachWindow(
            WindowId id,
            const WindowPresentationTarget& target,
            FramebufferSize framebufferSize) = 0;
        [[nodiscard]] virtual WindowPresentationResult RedrawWindow(
            WindowId id,
            FramebufferSize framebufferSize) = 0;
        virtual void DetachWindow(WindowId id) noexcept = 0;
        virtual void Shutdown() noexcept = 0;
    };
}
