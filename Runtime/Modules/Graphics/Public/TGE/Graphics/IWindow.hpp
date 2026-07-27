#pragma once

#include <string>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Graphics/WindowDescriptor.hpp"
#include "TGE/Graphics/WindowError.hpp"
#include "TGE/Graphics/WindowEvents.hpp"

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
        [[nodiscard]] virtual WindowDescriptor RequestedDescriptor() const = 0;
        [[nodiscard]] virtual WindowConfiguration EffectiveConfiguration()
            const = 0;
        [[nodiscard]] virtual WindowCapabilities Capabilities() const noexcept = 0;
        [[nodiscard]] virtual WindowLifecycleState LifecycleState()
            const noexcept = 0;

        [[nodiscard]] virtual std::string Title() const = 0;
        [[nodiscard]] virtual WindowRole Role() const noexcept = 0;
        [[nodiscard]] virtual std::optional<WindowId> ParentId()
            const noexcept = 0;
        [[nodiscard]] virtual WindowGeometry Geometry() const noexcept = 0;
        [[nodiscard]] virtual WindowState State() const noexcept = 0;
        [[nodiscard]] virtual bool IsVisible() const noexcept = 0;
        [[nodiscard]] virtual bool IsFocused() const noexcept = 0;
        [[nodiscard]] virtual bool IsInputEnabled() const noexcept = 0;

        virtual Task<WindowOperationResult> SetTitleAsync(
            std::string title) = 0;
        virtual Task<WindowOperationResult> SetLogicalBoundsAsync(
            LogicalBounds bounds) = 0;
        virtual Task<WindowOperationResult> SetStateAsync(
            WindowState state) = 0;
        virtual Task<WindowOperationResult> ShowAsync() = 0;
        virtual Task<WindowOperationResult> HideAsync() = 0;
        virtual Task<WindowOperationResult> RequestFocusAsync() = 0;
        virtual Task<WindowOperationResult> SetInputEnabledAsync(
            bool enabled) = 0;

        /**
         * @brief Ask the normal close policy to close this window.
         *
         * The request may be cancelled by application policy. Use
         * IWindowManager::DestroyWindowAsync for unconditional teardown.
         */
        virtual Task<WindowOperationResult> RequestCloseAsync() = 0;

        [[nodiscard]] virtual WindowSubscription SubscribeCloseRequested(
            WindowCloseRequestedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeClosed(
            WindowClosedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeMoved(
            WindowMovedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeResized(
            WindowResizedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeScaleChanged(
            WindowScaleChangedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeStateChanged(
            WindowStateChangedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeFocusChanged(
            WindowFocusChangedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeInputChanged(
            WindowInputChangedCallback callback) = 0;
        [[nodiscard]] virtual WindowSubscription SubscribeConfigurationChanged(
            WindowConfigurationChangedCallback callback) = 0;
    };
}
