#include <algorithm>
#include "Services/ServiceCollection.hpp"
#include "Services/ServiceProvider.hpp"

namespace TGE {

ServiceDescriptor ServiceCollection::GetDescriptorFor(std::type_index serviceTypeId) const
{
    if (registeredServices.contains(serviceTypeId))
        return registeredServices.at(serviceTypeId);

    if (serviceImplementations.contains(serviceTypeId))
    {
        const std::type_index interfaceTypeId = serviceImplementations.at(serviceTypeId);
        if (registeredServices.contains(interfaceTypeId))
            return registeredServices.at(interfaceTypeId);
    }

    throw std::domain_error(std::format("Unable to find a registered service descriptor for type \"{}\"", serviceTypeId.name()));
}

std::vector<ServiceDescriptor> ServiceCollection::GetRegisteredServices() const
{
    auto values = std::vector<ServiceDescriptor>();
    values.reserve(registeredServices.size());

    std::transform(begin(registeredServices), end(registeredServices), back_inserter(values),
        [](const std::pair<std::type_index, ServiceDescriptor>& pair) { return pair.second; });

    return values;
}

std::shared_ptr<ServiceProvider> ServiceCollection::BuildServiceProvider() const
{
    auto provider = new ServiceProvider(*this);
    return std::shared_ptr<ServiceProvider>(provider);
}

void ServiceCollection::Add(ServiceDescriptor descriptor)
{
    registeredServices.emplace(descriptor.GetServiceType(), descriptor);
    serviceImplementations.emplace(descriptor.GetImplementationType(), descriptor.GetServiceType());
}

} // namespace TGE