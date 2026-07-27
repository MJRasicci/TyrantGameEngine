/**
 * @file CliApplication.hpp
 * @brief Console-oriented composition over the generic Application host.
 */

#pragma once

#include <concepts>
#include <type_traits>

#include "TGE/Application/Application.hpp"
#include "TGE/Application/ApplicationState.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"

namespace TGE
{
    /**
     * @class CliApplication
     * @brief Adds process-signal shutdown policy to a generic Application.
     *
     * SIGINT and SIGTERM are translated into cooperative stop requests while
     * this application is running. Service hosting and lifecycle ordering
     * remain owned by the composed Application.
     */
    class TGE_API CliApplication final
    {
    public:
        CliApplication();
        ~CliApplication();

        static CliApplication Create();

        CliApplication(const CliApplication&) = delete;
        CliApplication& operator=(const CliApplication&) = delete;
        CliApplication(CliApplication&&) = delete;
        CliApplication& operator=(CliApplication&&) = delete;

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

        int Run() &;
        Task<int> RunAsync() &;

        bool RequestStop(int exitCode = 0) noexcept;
        ApplicationState GetState() const noexcept;

    private:
        Application application;
    };

    template<class TService>
        requires IService<TService> &&
                 std::derived_from<TService, IHostedService> &&
                 (!std::is_abstract_v<TService>)
    void CliApplication::AddHostedService() &
    {
        application.template AddHostedService<TService>();
    }
}
