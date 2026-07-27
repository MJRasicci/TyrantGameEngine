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
    class WindowSession;

    namespace detail
    {
        struct GuiApplicationConfiguration;
    }

    /**
     * @class GuiApplication
     * @brief Adds optional root-window policy to a generic Application.
     *
     * GuiApplication does not own a platform event loop. It composes the
     * generic host with IWindowManager when a root window is configured. With
     * no root configuration it does not require a window-manager registration.
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

        int Run() &;
        Task<int> RunAsync() &;

        bool RequestStop(int exitCode = 0) noexcept;
        ApplicationState GetState() const noexcept;

    private:
        Application application;
        std::shared_ptr<detail::GuiApplicationConfiguration> configuration;
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
