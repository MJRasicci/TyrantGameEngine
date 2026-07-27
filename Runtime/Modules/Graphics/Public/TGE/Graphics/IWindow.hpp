#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "TGE/Export.hpp"
#include "TGE/Graphics/WindowId.hpp"
#include "TGE/Graphics/WindowTypes.hpp"

namespace TGE
{
    /**
     * @brief Platform-neutral control and state surface for one window.
     */
    class TGE_API IWindow
    {
    public:
        virtual ~IWindow();

        [[nodiscard]] virtual WindowId Id() const noexcept = 0;
        [[nodiscard]] virtual std::string Title() const = 0;
        virtual void SetTitle(std::string_view title) = 0;

        [[nodiscard]] virtual WindowRole Role() const noexcept = 0;
        [[nodiscard]] virtual std::optional<WindowId> ParentId()
            const noexcept = 0;
        [[nodiscard]] virtual WindowBounds Bounds() const noexcept = 0;
        virtual void SetBounds(WindowBounds bounds) = 0;

        [[nodiscard]] virtual WindowState State() const noexcept = 0;
        virtual void SetState(WindowState state) = 0;

        [[nodiscard]] virtual bool IsVisible() const noexcept = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;

        /**
         * @brief Ask the normal close policy to close this window.
         *
         * The request may be cancelled by application policy. Use
         * IWindowManager::DestroyWindow for unconditional teardown.
         */
        virtual void RequestClose() = 0;
    };
}
