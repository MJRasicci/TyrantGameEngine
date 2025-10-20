#pragma once

#include <format>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "TGE/Services/ServiceLocator.hpp"

namespace TGE
{
    namespace detail
    {
        template<class TTag>
        auto ResolveDependency(ServiceLocator& locator, TTag tag)
        {
            if constexpr (std::is_same_v<TTag, LocatorDependency>)
            {
                return std::ref(locator);
            }
            else
            {
                return locator.template GetRequiredService<typename TTag::ServiceType>();
            }
        }

        template<class TService, class TImplementation, class Tuple, std::size_t... Indices>
        ServiceDescriptor::ActivationHandle InstantiateFromTuple(
            ServiceLocator& locator, const Tuple& tuple, std::index_sequence<Indices...>)
        {
            auto instance = std::make_shared<TImplementation>(
                ResolveDependency(locator, std::get<Indices>(tuple))...);
            std::shared_ptr<TService> service = instance;
            return std::shared_ptr<void>(service, service.get());
        }

        template<class TService>
        ServiceDescriptor::ActivationHandle WrapExistingInstance(const std::shared_ptr<TService>& existingInstance)
        {
            return std::shared_ptr<void>(existingInstance, existingInstance.get());
        }

        template<class TService, class TImplementation>
        ServiceDescriptor::ActivationHandle ActivateUsingTraits(ServiceLocator& locator, const ServiceDescriptor&)
        {
            auto dependencies = ServiceDependencyTraits<TImplementation>::Dependencies();
            constexpr std::size_t dependencyCount = std::tuple_size_v<decltype(dependencies)>;
            return InstantiateFromTuple<TService, TImplementation>(
                locator, dependencies, std::make_index_sequence<dependencyCount> {});
        }
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Singleton()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Singleton);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Scoped()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Scoped);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Transient()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Transient);
    }

    template<class TService>
        requires IService<TService>
    ServiceDescriptor ServiceDescriptor::Singleton(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Singleton, instance);
    }

    template<class TService>
        requires IService<TService>
    ServiceDescriptor ServiceDescriptor::Scoped(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Scoped, instance);
    }

    template<class TService>
        requires IService<TService>
    ServiceDescriptor ServiceDescriptor::Transient(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Transient, instance);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Singleton(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Singleton, std::move(factory));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Scoped(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Scoped, std::move(factory));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
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

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Create(ServiceLifetime lifetime)
    {
        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TImplementation),
            &detail::ActivateUsingTraits<TService, TImplementation>,
            {},
            {},
            &ServiceDescriptor::AdaptService<TService, TImplementation>,
            &ServiceDescriptor::AdaptImplementation<TImplementation>);
    }

    template<class TService>
        requires IService<TService>
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
            detail::WrapExistingInstance(instance),
            &ServiceDescriptor::AdaptService<TService, TService>,
            &ServiceDescriptor::AdaptImplementation<TService>);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
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

            return detail::WrapExistingInstance(produced);
        };

        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TImplementation),
            &detail::ActivateUsingTraits<TService, TImplementation>,
            std::move(wrapped),
            {},
            &ServiceDescriptor::AdaptService<TService, TImplementation>,
            &ServiceDescriptor::AdaptImplementation<TImplementation>);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
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
                                               ActivationHandle existingInstance,
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

