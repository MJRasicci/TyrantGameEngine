/**
 * @file ApplicationLifetime.hpp
 * @brief Cooperative application shutdown signaling.
 */

#pragma once

#include <memory>
#include <stop_token>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"

namespace TGE
{
    class Application;

    /**
     * @class ApplicationLifetime
     * @brief Coordinates cooperative shutdown for an Application.
     *
     * The first stop request wins and records the process exit code. Hosted
     * services can receive this object through dependency injection and request
     * shutdown without owning the application itself.
     */
    class TGE_API ApplicationLifetime final
    {
    public:
        /**
         * @brief Construct an unset application lifetime signal.
         */
        ApplicationLifetime();

        /**
         * @brief Release the shared shutdown state.
         */
        ~ApplicationLifetime();

        ApplicationLifetime(const ApplicationLifetime&) = delete;
        ApplicationLifetime& operator=(const ApplicationLifetime&) = delete;
        ApplicationLifetime(ApplicationLifetime&&) = delete;
        ApplicationLifetime& operator=(ApplicationLifetime&&) = delete;

        /**
         * @brief Token requested when application shutdown begins.
         */
        std::stop_token GetStoppingToken() const noexcept;

        /**
         * @brief Whether shutdown has been requested.
         */
        bool IsStopRequested() const noexcept;

        /**
         * @brief Request cooperative shutdown.
         * @param exitCode Process exit code returned by Application::Run.
         * @return true when this call initiated shutdown; false when a previous
         * request had already won.
         */
        bool RequestStop(int exitCode = 0) noexcept;

        /**
         * @brief Exit code supplied by the first stop request.
         */
        int GetExitCode() const noexcept;

    private:
        struct State;
        using WaitStartedCallback = void (*)(void*) noexcept;

        Task<void> WaitForStopAsync(
            void* context,
            WaitStartedCallback waitStarted);

        std::shared_ptr<State> state;

        friend class Application;
    };
}
