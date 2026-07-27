#pragma once

#include <functional>
#include <memory>

#include "TGE/Export.hpp"
#include "TGE/Graphics/WindowConfiguration.hpp"

namespace TGE
{
    enum class WindowCloseReason
    {
        ApplicationRequest,
        UserRequest,
        ParentClosed,
        PlatformRequest
    };

    class TGE_API WindowCloseRequestedEvent
    {
    public:
        WindowCloseRequestedEvent(
            WindowId window,
            WindowCloseReason reason) noexcept;

        [[nodiscard]] WindowId Window() const noexcept;
        [[nodiscard]] WindowCloseReason Reason() const noexcept;
        [[nodiscard]] bool IsCancelled() const noexcept;
        void Cancel() noexcept;

    private:
        WindowId window;
        WindowCloseReason reason;
        bool cancelled { false };
    };

    struct WindowClosedEvent
    {
        WindowId window;
        WindowCloseReason reason { WindowCloseReason::PlatformRequest };
    };

    struct WindowMovedEvent
    {
        WindowId window;
        LogicalPoint previous {};
        LogicalPoint current {};
    };

    struct WindowResizedEvent
    {
        WindowId window;
        WindowGeometry previous {};
        WindowGeometry current {};
    };

    struct WindowScaleChangedEvent
    {
        WindowId window;
        WindowGeometry previous {};
        WindowGeometry current {};
    };

    struct WindowStateChangedEvent
    {
        WindowId window;
        WindowState previous { WindowState::Normal };
        WindowState current { WindowState::Normal };
    };

    struct WindowFocusChangedEvent
    {
        WindowId window;
        bool focused { false };
    };

    struct WindowInputChangedEvent
    {
        WindowId window;
        bool enabled { true };
    };

    struct WindowConfigurationChangedEvent
    {
        WindowId window;
        WindowConfiguration previous {};
        WindowConfiguration current {};
    };

    /**
     * @brief Move-only RAII token for a window event subscription.
     *
     * Resetting or destroying a token prevents later callback invocation. A
     * callback already executing on the serialized window dispatcher is
     * allowed to finish.
     */
    class TGE_API WindowSubscription final
    {
    public:
        WindowSubscription() noexcept;
        explicit WindowSubscription(std::function<void()> unsubscribe);
        ~WindowSubscription();

        WindowSubscription(const WindowSubscription&) = delete;
        WindowSubscription& operator=(const WindowSubscription&) = delete;

        WindowSubscription(WindowSubscription&& other) noexcept;
        WindowSubscription& operator=(WindowSubscription&& other) noexcept;

        void Reset() noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        struct State;
        std::shared_ptr<State> state;
    };

    using WindowCloseRequestedCallback =
        std::function<void(WindowCloseRequestedEvent&)>;
    using WindowClosedCallback = std::function<void(const WindowClosedEvent&)>;
    using WindowMovedCallback = std::function<void(const WindowMovedEvent&)>;
    using WindowResizedCallback =
        std::function<void(const WindowResizedEvent&)>;
    using WindowScaleChangedCallback =
        std::function<void(const WindowScaleChangedEvent&)>;
    using WindowStateChangedCallback =
        std::function<void(const WindowStateChangedEvent&)>;
    using WindowFocusChangedCallback =
        std::function<void(const WindowFocusChangedEvent&)>;
    using WindowInputChangedCallback =
        std::function<void(const WindowInputChangedEvent&)>;
    using WindowConfigurationChangedCallback =
        std::function<void(const WindowConfigurationChangedEvent&)>;
}
