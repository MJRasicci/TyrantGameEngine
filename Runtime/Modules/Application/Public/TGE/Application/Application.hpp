/**
 * @file Application.hpp
 * @brief Concrete composition root and lifecycle owner for TGE applications.
 */

#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>

#include "TGE/Application/ApplicationState.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Services/ServiceCollection.hpp"

namespace TGE
{
    /**
     * @class Application
     * @brief Configures services and executes one application lifecycle.
     *
     * Application is intentionally concrete and single-use. Consumers compose
     * an application by registering ordinary and hosted services rather than
     * deriving from an engine host base class.
     */
    class TGE_API Application final
    {
    public:
        /**
         * @brief Construct an unconfigured application composition root.
         */
        Application();

        /**
         * @brief Request shutdown if execution is active and release owned state.
         */
        ~Application();

        /**
         * @brief Construct a new application composition root.
         */
        static Application Create();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /**
         * @brief Mutable service collection available before execution starts.
         * @throws std::logic_error after the application leaves Created state.
         */
        ServiceCollection& Services() &;

        /**
         * @brief Register a singleton service managed by this Application.
         *
         * Hosted services start in registration order and stop in reverse
         * successful-start order. The lifecycle designation belongs to the
         * Application rather than to the dependency-injection container.
         */
        template<class TService>
            requires IService<TService> &&
                     std::derived_from<TService, IHostedService> &&
                     (!std::is_abstract_v<TService>)
        void AddHostedService() &;

        /**
         * @brief Execute the complete lifecycle while blocking this thread.
         */
        int Run() &;

        /**
         * @brief Return the lazy asynchronous operation for the complete lifecycle.
         *
         * The Application must outlive the returned operation, and the operation
         * must be driven to completion.
         */
        Task<int> RunAsync() &;

        /**
         * @brief Request cooperative shutdown from any thread.
         */
        bool RequestStop(int exitCode = 0) noexcept;

        /**
         * @brief Current lifecycle state.
         */
        ApplicationState GetState() const noexcept;

    private:
        using HostedServiceResolver =
            std::function<std::shared_ptr<IHostedService>(ServiceLocator&)>;

        void RegisterHostedService(HostedServiceResolver resolver);

        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    template<class TService>
        requires IService<TService> &&
                 std::derived_from<TService, IHostedService> &&
                 (!std::is_abstract_v<TService>)
    void Application::AddHostedService() &
    {
        Services().template AddSingleton<TService>();
        RegisterHostedService(
            [](ServiceLocator& locator) -> std::shared_ptr<IHostedService>
            {
                return locator.template GetRequiredService<TService>();
            });
    }
}
