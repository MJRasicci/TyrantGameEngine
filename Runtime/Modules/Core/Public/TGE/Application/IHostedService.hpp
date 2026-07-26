/**
 * @file IHostedService.hpp
 * @brief Lifecycle contract for services managed by an Application.
 */

#pragma once

#include <stop_token>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"

namespace TGE
{
    /**
     * @class IHostedService
     * @brief Asynchronous startup and shutdown contract for application services.
     *
     * Applications start hosted services in registration order and stop every
     * successfully started service in reverse registration order.
     */
    class TGE_API IHostedService
    {
    public:
        /**
         * @brief Destroy the hosted service.
         */
        virtual ~IHostedService() = default;

        /**
         * @brief Start the service.
         * @param stopping Token requested when application shutdown begins.
         *
         * Completion indicates that startup has finished, not that the
         * service's entire useful lifetime has completed.
         */
        virtual Task<void> StartAsync(std::stop_token stopping) = 0;

        /**
         * @brief Stop the service and release lifecycle-owned resources.
         */
        virtual Task<void> StopAsync() = 0;
    };
}
