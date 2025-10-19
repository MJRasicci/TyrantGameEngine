#include <utility>

#include "Services/ServiceProvider.hpp"

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

    ServiceScope::ServiceScope(std::shared_ptr<ServiceProvider> rootProvider)
        : ServiceLocator(rootProvider->GetRegistry(), &rootProvider->singletonStorage, rootProvider.get(), rootProvider.get()),
          rootOwner(std::move(rootProvider))
    {
    }
}

