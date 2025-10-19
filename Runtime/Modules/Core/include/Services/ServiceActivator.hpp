/**
 * @file ServiceActivator.hpp
 * @brief Helpers that construct services from compile-time dependency traits.
 */

#pragma once

#include <memory>
#include <tuple>
#include <utility>

#include "Services/ServiceTraits.hpp"

namespace TGE
{
    class ServiceDescriptor;
    class ServiceLocator;

    /**
     * @class ServiceActivator
     * @brief Centralizes construction logic for registered services.
     */
class ServiceActivator
{
public:
        using ActivationResult = std::shared_ptr<void>;

        /**
         * @brief Build a service using its dependency traits.
         * @details
         * Resolves each dependency declared in ::TGE::ServiceDependencyTraits and forwards the
         * resulting arguments to the implementation constructor.
         */
        template<class TService, class TImplementation>
        static ActivationResult Activate(ServiceLocator& locator, const ServiceDescriptor& descriptor);

        /**
         * @brief Build a service from a custom factory delegate.
         * @details
         * Invokes the supplied factory and wraps the resulting pointer in a type-erased handle.
         */
        template<class TService, class TFactory>
        static ActivationResult Activate(ServiceLocator& locator, TFactory&& factory);

        /**
         * @brief Wrap an existing service instance in a type-erased handle.
         * @details
         * The returned shared pointer aliases the existing control block without copying the service.
         */
        template<class TService>
        static ActivationResult Activate(const std::shared_ptr<TService>& existingInstance);

    private:
        template<class TTag>
        static auto resolve(ServiceLocator& locator, TTag tag);

        template<class TService, class TImplementation, class Tuple, std::size_t... Indices>
        static ActivationResult createFromTuple(ServiceLocator& locator, const Tuple& tuple, std::index_sequence<Indices...>);
};
}

#include "Services/ServiceActivator.inl"

