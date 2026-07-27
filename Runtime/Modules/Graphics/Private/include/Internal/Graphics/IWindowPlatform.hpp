#pragma once

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
     * @brief Event receiver called exclusively on the desktop event runtime.
     */
    class IWindowPlatformEventSink
    {
    public:
        virtual ~IWindowPlatformEventSink() = default;

        virtual void OnPlatformConfigurationChanged(
            WindowId id,
            WindowConfiguration configuration) noexcept = 0;
        /**
         * @brief Ask the renderer to commit another frame for a native window.
         */
        virtual void OnPlatformRedrawRequested(
            WindowId id) noexcept
        {
            (void)id;
        }
        /**
         * @brief Release presentation state before native handles are invalid.
         */
        virtual void OnPlatformPresentationTargetInvalidating(
            WindowId id) noexcept
        {
            (void)id;
        }
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
     * Every method except SetEventSink is invoked on the shared desktop event
     * runtime thread. SetEventSink is a thread-safe lifecycle boundary: after
     * SetEventSink(nullptr) returns, the previous sink receives no new
     * callbacks and all callbacks that had already entered it have returned.
     *
     * Native event pumping is deliberately not part of this interface. One
     * process-level desktop pump is shared by windowing and input adapters.
     */
    class IWindowPlatform
    {
    public:
        virtual ~IWindowPlatform() = default;

        virtual void SetEventSink(
            IWindowPlatformEventSink* sink) noexcept = 0;

        /**
         * @brief Release platform-side window adapter state on the event thread.
         *
         * All live windows have already received DestroyWindow before this
         * method is called. Destruction of the C++ adapter may occur later on
         * another thread and therefore must not call thread-affine APIs.
         */
        virtual void Shutdown() noexcept = 0;

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
