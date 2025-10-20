#pragma once

#include <functional>
#include <tuple>
#include <type_traits>

#include "TGE/Services/ServiceLocator.hpp"

namespace TGE
{
    template<class TService, class TImplementation>
    ServiceActivator::ActivationResult ServiceActivator::Activate(ServiceLocator& locator, const ServiceDescriptor&)
    {
        auto dependencies = ServiceDependencyTraits<TImplementation>::Dependencies();
        constexpr std::size_t dependencyCount = std::tuple_size_v<decltype(dependencies)>;
        return createFromTuple<TService, TImplementation>(locator, dependencies, std::make_index_sequence<dependencyCount> {});
    }

    template<class TService, class TFactory>
    ServiceActivator::ActivationResult ServiceActivator::Activate(ServiceLocator& locator, TFactory&& factory)
    {
        auto instance = factory(locator);
        return ServiceActivator::Activate(instance);
    }

    template<class TService>
    ServiceActivator::ActivationResult ServiceActivator::Activate(const std::shared_ptr<TService>& existingInstance)
    {
        return std::shared_ptr<void>(existingInstance, existingInstance.get());
    }

    template<class TTag>
    auto ServiceActivator::resolve(ServiceLocator& locator, TTag tag)
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
    ServiceActivator::ActivationResult ServiceActivator::createFromTuple(ServiceLocator& locator, const Tuple& tuple, std::index_sequence<Indices...>)
    {
        auto instance = std::make_shared<TImplementation>(resolve(locator, std::get<Indices>(tuple))...);
        std::shared_ptr<TService> service = instance;
        return std::shared_ptr<void>(service, service.get());
    }
}

