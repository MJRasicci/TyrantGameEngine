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
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "TGE/Application/IHostedService.hpp"
#include "TGE/Export.hpp"
#include "TGE/Options/OptionsConcepts.hpp"
#include "TGE/Services/ServiceDescriptor.hpp"

namespace TGE
{
    template<OptionsType TOptions>
    class OptionsBuilder;

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
         * @brief Register a singleton service managed by Application lifecycle.
         *
         * Hosted services are started in registration order and stopped in
         * reverse registration order.
         */
        template<class TService>
            requires IService<TService> &&
                     std::derived_from<TService, IHostedService> &&
                     (!std::is_abstract_v<TService>)
        void AddHostedService();

        /**
         * @brief Test whether a service type already has a registration.
         */
        template<class TService>
            requires IService<TService>
        bool Contains() const noexcept;

        /**
         * @brief Add a singleton only when the service type is unregistered.
         * @return true when the service was added.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        bool TryAddSingleton();

        /**
         * @brief Add an existing singleton only when its type is unregistered.
         * @return true when the service was added.
         */
        template<class TService>
            requires IService<TService>
        bool TryAddSingleton(const std::shared_ptr<TService>& instance);

        /**
         * @brief Add a transient only when the service type is unregistered.
         * @return true when the service was added.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        bool TryAddTransient();

        /**
         * @brief Register or reopen the singleton monitor for an options type.
         *
         * Repeated calls compose additional sources and validators onto the
         * same monitor, allowing independent subsystems to contribute settings.
         */
        template<OptionsType TOptions>
        OptionsBuilder<TOptions> AddOptions(TOptions defaults = {});

        /**
         * @brief Create the root service provider for the configured services.
         */
        std::shared_ptr<ServiceProvider> BuildServiceProvider();

    private:
        /**
         * @brief Insert a descriptor into the registry with duplicate detection.
         */
        void Register(ServiceDescriptor descriptor);

        void RegisterHostedService(
            ServiceDescriptor descriptor,
            std::function<std::shared_ptr<IHostedService>(ServiceLocator&)> factory);

        bool Contains(std::type_index serviceType) const noexcept;

        /**
         * @brief Accumulates descriptors prior to provider construction.
         */
        std::unique_ptr<detail::ServiceRegistry> registry;
        std::unordered_map<std::type_index, std::shared_ptr<void>>
            optionsMonitors;
    };
}

#include "TGE/Services/ServiceCollection.inl"
