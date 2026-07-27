/**
 * @file GuiApplication.hpp
 * @brief Window-aware composition over the generic Application host.
 */

#pragma once

#include <concepts>
#include <memory>
#include <type_traits>

#include "TGE/Application/Application.hpp"
#include "TGE/Application/ApplicationState.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Graphics/WindowDescriptor.hpp"

namespace TGE
{
    class GuiApplicationContext;
    class WindowSession;

    namespace detail
    {
        struct GuiApplicationConfiguration;
    }

    /**
     * @class GuiApplication
     * @brief Adds optional desktop and root-window policy to an Application.
     *
     * The default backend's event queue is driven by Run on its calling thread,
     * not by a hosted service. Root creation and root-close shutdown remain
     * application composition policy above Graphics and Input. With no root
     * configuration, no window or input service registration is required.
     */
    class TGE_API GuiApplication final
    {
    public:
        GuiApplication();
        ~GuiApplication();

        static GuiApplication Create();

        GuiApplication(const GuiApplication&) = delete;
        GuiApplication& operator=(const GuiApplication&) = delete;
        GuiApplication(GuiApplication&&) = delete;
        GuiApplication& operator=(GuiApplication&&) = delete;

        /**
         * @brief Mutable service collection available before execution starts.
         */
        ServiceCollection& Services() &;

        /**
         * @brief Register a singleton hosted service with the composed host.
         */
        template<class TService>
            requires IService<TService> &&
                     std::derived_from<TService, IHostedService> &&
                     (!std::is_abstract_v<TService>)
        void AddHostedService() &;

        /**
         * @brief Register Tyrant's selected private desktop backend.
         *
         * The backend remains an implementation detail: callers receive only
         * IWindowManager, IInputManager, and the window/input bridge through
         * dependency injection. This operation may be called once before
         * execution starts.
         */
        GuiApplication& UseDefaultDesktopBackend() &;

        /**
         * @brief Configure the window whose closure stops this application.
         *
         * The root is created before user-registered hosted services start.
         */
        GuiApplication& ConfigureRootWindow(WindowDescriptor descriptor) &;

        /**
         * @brief Remove a previously configured root window.
         */
        GuiApplication& ClearRootWindow() &;

        /**
         * @brief Current root session, or null before creation/after teardown.
         */
        [[nodiscard]] std::shared_ptr<WindowSession> RootSession() const;

        /**
         * @brief Run the GUI lifecycle and drive its desktop event queue here.
         *
         * When a desktop backend is configured, this calling thread becomes
         * the platform event thread. Callers never need to marshal window or
         * input operations themselves. Native UI constraints still apply to
         * this entry point: for example, the default SDL backend requires the
         * process main thread on Apple platforms and reports a startup error
         * otherwise.
         */
        int Run() &;
        Task<int> RunAsync() &;

        bool RequestStop(int exitCode = 0) noexcept;
        ApplicationState GetState() const noexcept;

    private:
        Application application;
        std::shared_ptr<detail::GuiApplicationConfiguration> configuration;
        std::shared_ptr<GuiApplicationContext> context;
    };

    template<class TService>
        requires IService<TService> &&
                 std::derived_from<TService, IHostedService> &&
                 (!std::is_abstract_v<TService>)
    void GuiApplication::AddHostedService() &
    {
        application.template AddHostedService<TService>();
    }
}
