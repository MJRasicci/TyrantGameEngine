#include <utility>

#include "TGE/Application/IHostedService.hpp"
#include "TGE/Services/ServiceProvider.hpp"

#include "Internal/Services/ServiceRegistry.hpp"

namespace TGE
{
    ServiceProvider::ServiceProvider(std::shared_ptr<detail::ServiceRegistry> registry)
        : ServiceLocator(std::move(registry), &singletonStorage, this, nullptr)
    {
    }

    std::shared_ptr<ServiceScope> ServiceProvider::CreateScope()
    {
        return std::shared_ptr<ServiceScope>(new ServiceScope(shared_from_this()));
    }

    std::vector<std::shared_ptr<IHostedService>> ServiceProvider::GetHostedServices()
    {
        std::vector<std::shared_ptr<IHostedService>> services;
        services.reserve(GetRegistry()->hostedServiceFactories.size());

        for (const auto& factory : GetRegistry()->hostedServiceFactories)
        {
            services.emplace_back(factory(*this));
        }

        return services;
    }

    ServiceScope::ServiceScope(std::shared_ptr<ServiceProvider> rootProvider)
        : ServiceLocator(rootProvider->GetRegistry(), &rootProvider->singletonStorage, rootProvider.get(), rootProvider.get()),
          rootOwner(std::move(rootProvider))
    {
    }
}
