/**
 * @file ServiceHost.hpp
 * @brief Defines the ServiceHost class for hosting services in the Tyrant Game Engine container.
 *
 * This file contains the ServiceHost class, which serves as a base class for hosting
 * services within the Tyrant Game Engine container. It outlines the structure and lifecycle of services,
 * including initialization, configuration, start, and stop phases. The ServiceHost class
 * is designed to be extended by derived classes which provide specific service logic.
 */

#pragma once

#include "Export.hpp"
#include "Services/ServiceCollection.hpp"
#include "Services/ServiceProvider.hpp"
#include "Logging/Logger.hpp"

namespace TGE {

//===========================================================================//
//=======> ServiceHost Class <===============================================//
//===========================================================================//

/**
 * @class ServiceHost
 * @brief Represents the host for services managed by the Tyrant Game Engine container.
 *
 * ServiceHost is a base class for hosting services. It provides a framework
 * for service lifecycle management including initialization, start, and stop phases.
 */
class TGE_API ServiceHost
{
public:
    /**
     * @brief Starts the service host.
     *
     * This method starts the service execution by calling the Configure, OnStart,
     * and then OnStop methods in sequence.
     */
    void Start();

protected:
    std::shared_ptr<ServiceProvider> provider;

    /**
     * @brief Create a new service scope from the root provider.
     */
    std::shared_ptr<ServiceScope> CreateScope();

    /**
     * @brief Constructor for ServiceHost.
     *
     * Protected constructor to prevent instantiation of the base class,
     * promoting the use of derived classes.
     */
    ServiceHost();

    /**
     * @brief Pure virtual method for service configuration.
     *
     * This method should be overridden in derived classes to provide
     * specific configuration logic.
     */
    virtual void ConfigureServices(ServiceCollection& services) = 0;

    /**
     * @brief Virtual method called at the start of the service.
     *
     * Can be overridden in derived classes to include logic that should
     * execute when the service starts.
     */
    virtual void OnStart();

    /**
     * @brief Virtual method called at the stop of the service.
     *
     * Can be overridden in derived classes to include logic that should
     * execute when the service stops.
     */
    virtual void OnStop();

private:
    std::shared_ptr<Logger<ServiceHost>> logger;
};

} // namespace TGE
