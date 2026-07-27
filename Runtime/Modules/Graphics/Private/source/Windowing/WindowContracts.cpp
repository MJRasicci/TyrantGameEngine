#include "TGE/Graphics/IWindow.hpp"
#include "TGE/Graphics/IWindowManager.hpp"

#include <atomic>
#include <utility>

namespace TGE
{
    struct WindowSubscription::State
    {
        explicit State(std::function<void()> unsubscribe)
            : unsubscribe(std::move(unsubscribe))
        {
        }

        std::atomic<bool> active { true };
        std::function<void()> unsubscribe;
    };

    IWindow::~IWindow() = default;
    IWindowManager::~IWindowManager() = default;

    WindowCloseRequestedEvent::WindowCloseRequestedEvent(
        WindowId window,
        WindowCloseReason reason) noexcept
        : window(window),
          reason(reason)
    {
    }

    WindowId WindowCloseRequestedEvent::Window() const noexcept
    {
        return window;
    }

    WindowCloseReason WindowCloseRequestedEvent::Reason() const noexcept
    {
        return reason;
    }

    bool WindowCloseRequestedEvent::IsCancelled() const noexcept
    {
        return cancelled;
    }

    void WindowCloseRequestedEvent::Cancel() noexcept
    {
        cancelled = true;
    }

    WindowSubscription::WindowSubscription() noexcept = default;

    WindowSubscription::WindowSubscription(std::function<void()> unsubscribe)
        : state(std::make_shared<State>(std::move(unsubscribe)))
    {
    }

    WindowSubscription::~WindowSubscription()
    {
        Reset();
    }

    WindowSubscription::WindowSubscription(WindowSubscription&& other) noexcept
        : state(std::move(other.state))
    {
    }

    WindowSubscription& WindowSubscription::operator=(
        WindowSubscription&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            state = std::move(other.state);
        }
        return *this;
    }

    void WindowSubscription::Reset() noexcept
    {
        auto current = std::exchange(state, {});
        if (!current || !current->active.exchange(false))
        {
            return;
        }

        try
        {
            if (current->unsubscribe)
            {
                current->unsubscribe();
            }
        }
        catch (...)
        {
            // Subscription destruction is a best-effort, noexcept operation.
        }
    }

    WindowSubscription::operator bool() const noexcept
    {
        return state && state->active.load();
    }
}
