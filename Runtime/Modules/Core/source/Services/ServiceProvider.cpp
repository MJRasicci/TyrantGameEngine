#include "Services/ServiceProvider.hpp"

namespace TGE {

    ServiceProvider::ServiceProvider(ServiceCollection services)
        : ServiceLocator(services)
    { }

    std::shared_ptr<ServiceScope> ServiceProvider::CreateScope()
    {
        auto scope = new ServiceScope(*this);
        return std::shared_ptr<ServiceScope>(scope);
    }

    ServiceScope::ServiceScope(ServiceLocator& provider)
        : ServiceLocator(provider)
    {
        for (auto& service : services.GetRegisteredServices())
        {
            if (service.GetServiceLifetime() == ServiceLifetime::Scoped)
                instances.insert({ service.GetServiceType(), service.BuildInstance(this) });
        }
    }

} // namespace TGE