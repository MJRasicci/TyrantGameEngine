#pragma once

#include <chrono>
#include <expected>
#include <string>

#include "TGE/Graphics/WindowConfiguration.hpp"
#include "TGE/Graphics/WindowDescriptor.hpp"
#include "TGE/Graphics/WindowError.hpp"
#include "TGE/Graphics/WindowEvents.hpp"

namespace TGE::Internal
{
    struct WindowPlatformState
    {
        WindowConfiguration configuration;
        WindowCapabilities capabilities;
    };

    struct WindowPlatformMutation
    {
        WindowOperationStatus status { WindowOperationStatus::Applied };
        WindowConfiguration configuration;
    };

    using WindowPlatformCreateResult =
        std::expected<WindowPlatformState, WindowError>;
    using WindowPlatformMutationResult =
        std::expected<WindowPlatformMutation, WindowError>;

    /**
     * @brief Event receiver called exclusively on the platform dispatcher.
     */
    class IWindowPlatformEventSink
    {
    public:
        virtual ~IWindowPlatformEventSink() = default;

        virtual void OnPlatformConfigurationChanged(
            WindowId id,
            WindowConfiguration configuration) noexcept = 0;
        /**
         * @brief Run close-request policy and report whether closing may proceed.
         *
         * Native backends call this before accepting an operating-system close
         * request. A true result means the backend may destroy the native
         * window and must subsequently report OnPlatformClosed.
         */
        [[nodiscard]] virtual bool OnPlatformCloseRequested(
            WindowId id,
            WindowCloseReason reason) noexcept = 0;
        virtual void OnPlatformClosed(
            WindowId id,
            WindowCloseReason reason) noexcept = 0;
    };

    /**
     * @brief Private service-provider interface implemented by native backends.
     *
     * Except for WakeEventLoop, every method is invoked on one serialized
     * platform thread. PumpEvents may block for at most maxWait and delivers
     * events to the configured sink before returning.
     */
    class IWindowPlatform
    {
    public:
        virtual ~IWindowPlatform() = default;

        virtual void SetEventSink(
            IWindowPlatformEventSink* sink) noexcept = 0;
        virtual void WakeEventLoop() noexcept = 0;
        virtual void PumpEvents(
            std::chrono::milliseconds maxWait) noexcept = 0;

        [[nodiscard]] virtual WindowPlatformCreateResult CreateWindow(
            WindowId id,
            const WindowDescriptor& descriptor) = 0;
        [[nodiscard]] virtual WindowOperationResult DestroyWindow(
            WindowId id) = 0;
        [[nodiscard]] virtual WindowPlatformMutationResult SetTitle(
            WindowId id,
            std::string title) = 0;
        [[nodiscard]] virtual WindowPlatformMutationResult SetLogicalBounds(
            WindowId id,
            LogicalBounds bounds) = 0;
        [[nodiscard]] virtual WindowPlatformMutationResult SetState(
            WindowId id,
            WindowState state) = 0;
        [[nodiscard]] virtual WindowPlatformMutationResult SetVisible(
            WindowId id,
            bool visible) = 0;
        [[nodiscard]] virtual WindowPlatformMutationResult RequestFocus(
            WindowId id) = 0;
        [[nodiscard]] virtual WindowPlatformMutationResult SetInputEnabled(
            WindowId id,
            bool enabled) = 0;
        [[nodiscard]] virtual WindowOperationResult RequestClose(
            WindowId id) = 0;
    };
}
