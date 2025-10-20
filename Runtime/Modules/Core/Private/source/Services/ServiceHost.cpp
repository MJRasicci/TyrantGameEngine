#include <memory>
#include <stdexcept>

#include "TGE/Logging/Logger.hpp"
#include "TGE/Services/ServiceHost.hpp"

#include "Internal/Logging/GlobalLogger.hpp"

namespace TGE {

//===========================================================================//
//=======> Constructor <=====================================================//
//===========================================================================//

ServiceHost::ServiceHost()
{
    // Constructor implementation
    // This is where you might perform initialization tasks specific to the ServiceHost.
}

//===========================================================================//
//=======> Public Methods <==================================================//
//===========================================================================//

void ServiceHost::Start()
{
    // Initialize service collection and add logging services
    auto services = ServiceCollection();
    services.AddSingleton<GlobalLogger>();
    services.AddSingleton<ILogDispatcher, GlobalLogger>([](ServiceLocator& locator)
    {
        auto dispatcher = locator.GetRequiredService<GlobalLogger>();
        return std::static_pointer_cast<ILogDispatcher>(dispatcher);
    });
    services.AddTransient<Logger<ServiceHost>>();

    // Let derived class configure application services.
    ConfigureServices(services);

    // Build service provider
    provider = services.BuildServiceProvider();
    logger = provider->GetRequiredService<Logger<ServiceHost>>();
    logger->Debug("Configured ServiceProvider");

    // Starts the service host
    logger->Debug("Starting Host");
    OnStart();

    // Application loop...
    // This is where the main functionality of the service would typically be executed.

    // Stops the service host after execution.
    OnStop();
    logger->Debug("Host Stopped");
}

//===========================================================================//
//=======> Protected Methods <===============================================//
//===========================================================================//

void ServiceHost::OnStart()
{
    // Default implementation of OnStart.
    // This method can be overridden in derived classes to include logic for service start.
}

void ServiceHost::OnStop()
{
    // Default implementation of OnStop.
    // This method can be overridden in derived classes to include logic for service stop.
}

std::shared_ptr<ServiceScope> ServiceHost::CreateScope()
{
    if (!provider)
        throw std::domain_error("ServiceProvider has not been initialized.");

    return provider->CreateScope();
}

} // namespace TGE
