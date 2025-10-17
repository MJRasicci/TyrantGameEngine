#pragma once

#include <format>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "Services/ServiceActivator.hpp"
#include "Services/ServiceLocator.hpp"

namespace TGE
{
    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Singleton()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Singleton);
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Scoped()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Scoped);
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Transient()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Transient);
    }

    template<IService TService>
    ServiceDescriptor ServiceDescriptor::Singleton(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Singleton, instance);
    }

    template<IService TService>
    ServiceDescriptor ServiceDescriptor::Scoped(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Scoped, instance);
    }

    template<IService TService>
    ServiceDescriptor ServiceDescriptor::Transient(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Transient, instance);
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Singleton(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Singleton, std::move(factory));
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Scoped(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Scoped, std::move(factory));
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Transient(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Transient, std::move(factory));
    }

    inline ServiceDescriptor::ActivationHandle ServiceDescriptor::Activate(ServiceLocator& locator) const
    {
        if (existingInstance)
        {
            return existingInstance;
        }

        if (factory)
        {
            auto result = factory(locator);

            if (!result)
            {
                throw std::domain_error(std::format(
                    "Factory for service \"{}\" returned a null pointer.", serviceType.name()));
            }

            return result;
        }

        if (!activator)
        {
            throw std::domain_error(std::format(
                "Descriptor for service \"{}\" does not provide an activator.", serviceType.name()));
        }

        return activator(locator, *this);
    }

    template<class TService>
    std::shared_ptr<TService> ServiceDescriptor::CastToService(const ActivationHandle& instance) const
    {
        if (!instance)
        {
            return nullptr;
        }

        const std::type_index requested = typeid(TService);

        if (requested != serviceType && requested != implementationType)
        {
            throw std::domain_error(std::format(
                "Cannot cast service registration \"{}\" to requested type \"{}\".",
                serviceType.name(), requested.name()));
        }

        void* adjusted = requested == serviceType
            ? serviceAdapter(instance.get())
            : implementationAdapter(instance.get());

        return std::shared_ptr<TService>(instance, static_cast<TService*>(adjusted));
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Create(ServiceLifetime lifetime)
    {
        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TImplementation),
            &ServiceActivator::Activate<TService, TImplementation>,
            {},
            {},
            &ServiceDescriptor::AdaptService<TService, TImplementation>,
            &ServiceDescriptor::AdaptImplementation<TImplementation>);
    }

    template<IService TService>
    ServiceDescriptor ServiceDescriptor::Create(ServiceLifetime lifetime, const std::shared_ptr<TService>& instance)
    {
        if (!instance)
        {
            throw std::domain_error(std::format(
                "Attempted to register a null existing instance for service \"{}\".",
                typeid(TService).name()));
        }

        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TService),
            nullptr,
            {},
            ServiceActivator::Activate(instance),
            &ServiceDescriptor::AdaptService<TService, TService>,
            &ServiceDescriptor::AdaptImplementation<TService>);
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    ServiceDescriptor ServiceDescriptor::Create(ServiceLifetime lifetime, std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        if (!factory)
        {
            throw std::domain_error(std::format(
                "Factory registration for service \"{}\" cannot be null.",
                typeid(TService).name()));
        }

        FactoryDelegate wrapped = [inner = std::move(factory)](ServiceLocator& locator)
        {
            auto produced = inner(locator);

            if (!produced)
            {
                throw std::domain_error("Service factory returned a null instance.");
            }

            return ServiceActivator::Activate(produced);
        };

        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TImplementation),
            &ServiceActivator::Activate<TService, TImplementation>,
            std::move(wrapped),
            {},
            &ServiceDescriptor::AdaptService<TService, TImplementation>,
            &ServiceDescriptor::AdaptImplementation<TImplementation>);
    }

    template<IService TService, IServiceImplementation<TService> TImplementation>
    void* ServiceDescriptor::AdaptService(void* instance) noexcept
    {
        auto implementation = static_cast<TImplementation*>(instance);

        if constexpr (std::is_same_v<TService, TImplementation>)
        {
            return implementation;
        }
        else
        {
            return static_cast<TService*>(implementation);
        }
    }

    template<class TImplementation>
    void* ServiceDescriptor::AdaptImplementation(void* instance) noexcept
    {
        return static_cast<TImplementation*>(instance);
    }

    inline ServiceDescriptor::ServiceDescriptor(ServiceLifetime lifetime,
                                               std::type_index serviceType,
                                               std::type_index implementationType,
                                               ActivationDelegate activator,
                                               FactoryDelegate factory,
                                               ServiceActivator::ActivationResult existingInstance,
                                               PointerAdapter serviceAdapter,
                                               PointerAdapter implementationAdapter)
        : lifetime(lifetime),
          serviceType(serviceType),
          implementationType(implementationType),
          activator(activator),
          factory(std::move(factory)),
          existingInstance(existingInstance),
          serviceAdapter(serviceAdapter),
          implementationAdapter(implementationAdapter)
    {
    }
}

