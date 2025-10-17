#include "Services/ServiceLocator.hpp"
#include "Services/ServiceCollection.hpp"

namespace TGE {

ServiceLocator::ServiceLocator(ServiceLocator& locator)
    : services(locator.services), instances(locator.instances)
{ }

ServiceLocator::ServiceLocator(ServiceCollection& services) : services(services)
{
    for (auto& service : services.GetRegisteredServices())
    {
        if (service.GetServiceLifetime() == ServiceLifetime::Singleton)
            instances.emplace(service.GetServiceType(), service.BuildInstance(this));
    }
}

} // namespace