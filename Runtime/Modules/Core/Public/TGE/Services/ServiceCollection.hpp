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

#include "TGE/Export.hpp"
#include "TGE/Services/ServiceDescriptor.hpp"

namespace TGE
{
    class ServiceLocator;
    class ServiceProvider;
    namespace detail { struct ServiceRegistry; }

    /**
     * @class ServiceCollection
     * @brief Mutable registry used to configure the service provider.
     */
    class TGE_API ServiceCollection final
    {
    public:
        ServiceCollection();
        ~ServiceCollection();

        /**
         * @brief Register a singleton using the default activation strategy.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        void AddSingleton();

        /**
         * @brief Register a scoped service using the default activation strategy.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        void AddScoped();

        /**
         * @brief Register a transient service using the default activation strategy.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        void AddTransient();

        /**
         * @brief Register a singleton that reuses an existing instance.
         */
        template<class TService>
            requires IService<TService>
        void AddSingleton(const std::shared_ptr<TService>& instance);

        /**
         * @brief Register a scoped service that reuses an existing instance.
         */
        template<class TService>
            requires IService<TService>
        void AddScoped(const std::shared_ptr<TService>& instance);

        /**
         * @brief Register a transient service that reuses an existing instance.
         */
        template<class TService>
            requires IService<TService>
        void AddTransient(const std::shared_ptr<TService>& instance);

        /**
         * @brief Register a singleton that resolves instances via a custom factory.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        void AddSingleton(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Register a scoped service that resolves instances via a custom factory.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        void AddScoped(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Register a transient service that resolves instances via a custom factory.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        void AddTransient(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Create the root service provider for the configured services.
         */
        std::shared_ptr<ServiceProvider> BuildServiceProvider();

    private:
        /**
         * @brief Insert a descriptor into the registry with duplicate detection.
         */
        void Register(ServiceDescriptor descriptor);

        /**
         * @brief Accumulates descriptors prior to provider construction.
         */
        std::unique_ptr<detail::ServiceRegistry> registry;
    };
}

#include "TGE/Services/ServiceCollection.inl"
