#include <format>
#include <stdexcept>
#include <utility>

#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceProvider.hpp"

#include "Internal/Services/ServiceRegistry.hpp"

namespace TGE
{
    ServiceCollection::ServiceCollection()
        : registry(std::make_unique<detail::ServiceRegistry>())
    {
    }

    ServiceCollection::~ServiceCollection() = default;

    std::shared_ptr<ServiceProvider> ServiceCollection::BuildServiceProvider()
    {
        auto sharedRegistry = std::make_shared<detail::ServiceRegistry>();
        sharedRegistry->descriptors = std::move(registry->descriptors);
        sharedRegistry->implementationLookup = std::move(registry->implementationLookup);
        sharedRegistry->hostedServiceFactories =
            std::move(registry->hostedServiceFactories);

        return std::shared_ptr<ServiceProvider>(new ServiceProvider(std::move(sharedRegistry)));
    }

    void ServiceCollection::Register(ServiceDescriptor descriptor)
    {
        const auto serviceType = descriptor.GetServiceType();
        const auto implementationType = descriptor.GetImplementationType();

        if (registry->descriptors.contains(serviceType))
        {
            throw std::domain_error(std::format(
                "Service type \"{}\" has already been registered.", serviceType.name()));
        }

        if (registry->implementationLookup.contains(implementationType))
        {
            throw std::domain_error(std::format(
                "Implementation type \"{}\" has already been associated with another service.",
                implementationType.name()));
        }

        registry->descriptors.emplace(serviceType, std::move(descriptor));
        registry->implementationLookup.emplace(implementationType, serviceType);
    }

    void ServiceCollection::RegisterHostedService(
        ServiceDescriptor descriptor,
        std::function<std::shared_ptr<IHostedService>(ServiceLocator&)> factory)
    {
        Register(std::move(descriptor));
        registry->hostedServiceFactories.emplace_back(std::move(factory));
    }

    bool ServiceCollection::Contains(std::type_index serviceType) const noexcept
    {
        return registry->descriptors.contains(serviceType);
    }
}
