/**
 * @file ServiceCollection.hpp
 * @brief Registry used to configure the dependency injection container.
 */

#pragma once

#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "Export.hpp"
#include "Services/ServiceDescriptor.hpp"
#include "Services/ServiceRegistry.hpp"

namespace TGE
{
    class ServiceLocator;
    class ServiceProvider;

    /**
     * @class ServiceCollection
     * @brief Mutable registry used to configure the service provider.
     */
    class TGE_API ServiceCollection final
    {
    public:
        /**
         * @brief Register a singleton using the default activation strategy.
         */
        template<IService TService, IServiceImplementation<TService> TImplementation = TService>
        void AddSingleton();

        /**
         * @brief Register a scoped service using the default activation strategy.
         */
        template<IService TService, IServiceImplementation<TService> TImplementation = TService>
        void AddScoped();

        /**
         * @brief Register a transient service using the default activation strategy.
         */
        template<IService TService, IServiceImplementation<TService> TImplementation = TService>
        void AddTransient();

        /**
         * @brief Register a singleton that reuses an existing instance.
         */
        template<IService TService>
        void AddSingleton(const std::shared_ptr<TService>& instance);

        /**
         * @brief Register a scoped service that reuses an existing instance.
         */
        template<IService TService>
        void AddScoped(const std::shared_ptr<TService>& instance);

        /**
         * @brief Register a transient service that reuses an existing instance.
         */
        template<IService TService>
        void AddTransient(const std::shared_ptr<TService>& instance);

        /**
         * @brief Register a singleton that resolves instances via a custom factory.
         */
        template<IService TService, IServiceImplementation<TService> TImplementation = TService>
        void AddSingleton(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Register a scoped service that resolves instances via a custom factory.
         */
        template<IService TService, IServiceImplementation<TService> TImplementation = TService>
        void AddScoped(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Register a transient service that resolves instances via a custom factory.
         */
        template<IService TService, IServiceImplementation<TService> TImplementation = TService>
        void AddTransient(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Create the root service provider for the configured services.
         */
        std::shared_ptr<ServiceProvider> BuildServiceProvider();

        /**
         * @brief Read-only view of the registered descriptors.
         */
        const detail::ServiceRegistry& GetRegistry() const noexcept { return registry; }

    private:
        /**
         * @brief Insert a descriptor into the registry with duplicate detection.
         */
        void Register(ServiceDescriptor descriptor);

        /**
         * @brief Accumulates descriptors prior to provider construction.
         */
        detail::ServiceRegistry registry;
    };
}

#include "Services/ServiceCollection.inl"
