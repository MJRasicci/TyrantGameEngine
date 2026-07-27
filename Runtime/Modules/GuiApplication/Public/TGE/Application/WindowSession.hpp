/**
 * @file WindowSession.hpp
 * @brief Associates a window with dependency-injection and application lifetimes.
 */

#pragma once

#include <memory>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"

namespace TGE
{
    class ApplicationLifetime;
    class IWindow;
    class IWindowManager;
    class ServiceScope;

    /**
     * @brief Defines how window closure affects the associated service scope.
     */
    enum class WindowScopePolicy
    {
        /**
         * @brief The session uses a parent session's scope and never ends it.
         */
        Inherited,

        /**
         * @brief The session uses a caller-owned scope and never ends it.
         */
        External,

        /**
         * @brief Window closure ends the scope; scope ending destroys the window.
         */
        WindowOwned
    };

    struct WindowSessionOptions
    {
        WindowScopePolicy scopePolicy { WindowScopePolicy::WindowOwned };
        bool stopApplicationOnClose { false };
    };

    /**
     * @class WindowSession
     * @brief Lifetime bridge between one IWindow and one ServiceScope.
     *
     * Scope cleanup always destroys a still-live window. A window-owned scope
     * is ended asynchronously after final window closure, away from the
     * platform event callback. Inherited and external scopes remain under
     * their existing owners.
     */
    class TGE_API WindowSession final
    {
    public:
        /**
         * @brief Bind a live window to a service scope.
         *
         * lifetime is required only when stopApplicationOnClose is true.
         */
        static std::shared_ptr<WindowSession> Create(
            std::shared_ptr<IWindowManager> manager,
            std::shared_ptr<IWindow> window,
            std::shared_ptr<ServiceScope> scope,
            WindowSessionOptions options = {},
            std::shared_ptr<ApplicationLifetime> lifetime = {});

        ~WindowSession();

        WindowSession(const WindowSession&) = delete;
        WindowSession& operator=(const WindowSession&) = delete;
        WindowSession(WindowSession&&) = delete;
        WindowSession& operator=(WindowSession&&) = delete;

        [[nodiscard]] std::shared_ptr<IWindow> Window() const noexcept;
        [[nodiscard]] std::shared_ptr<ServiceScope> Scope() const noexcept;
        [[nodiscard]] WindowScopePolicy ScopePolicy() const noexcept;

        /**
         * @brief End this session explicitly.
         *
         * Window-owned sessions end their scope. Inherited and external
         * sessions destroy only their window.
         */
        Task<void> EndAsync();

    private:
        struct State;

        explicit WindowSession(std::shared_ptr<State> state);

        std::shared_ptr<State> state;
    };
}
