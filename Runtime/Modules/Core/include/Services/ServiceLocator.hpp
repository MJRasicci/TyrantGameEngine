#pragma once

#include <memory>
#include "Export.hpp"
#include "Services/Service.hpp"
#include "Services/ServiceDescriptor.hpp"
#include "Services/ServiceCollection.hpp"

namespace TGE
{
    class TGE_API ServiceLocator
    {
    public:
        template<IService TService>
        std::shared_ptr<TService> GetRequiredService()
        {
            auto service = TryGetService<TService>();

            if (service == nullptr)
                throw std::domain_error(std::format("Unable to locate service \"{}\"", typeid(TService).name()));

            return service;
        }

        template<IService TService>
        std::shared_ptr<TService> TryGetService()
        {
            ServiceDescriptor descriptor = services.GetDescriptorFor<TService>();
            std::type_index serviceTypeId = descriptor.GetServiceType();
            std::shared_ptr<std::any> serviceRef;

            if (descriptor.GetServiceLifetime() == ServiceLifetime::Transient)
                serviceRef = descriptor.BuildInstance(this);
            else if (instances.contains(serviceTypeId))
                serviceRef = instances.at(serviceTypeId);
            else
                return nullptr;

            return descriptor.CastToService<TService>(serviceRef);
        }

    protected:
        ServiceLocator(ServiceCollection& services);

        ServiceLocator(ServiceLocator& locator);

        ServiceCollection services;
        //ServiceFactory factory;
        std::unordered_map<std::type_index, std::shared_ptr<std::any>> instances;
    };
}
