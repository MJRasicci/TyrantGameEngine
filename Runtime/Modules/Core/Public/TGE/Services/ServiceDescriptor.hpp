/**
 * @file ServiceDescriptor.hpp
 * @brief Metadata describing service registrations and activator delegates.
 */

#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>

#include "TGE/Export.hpp"
#include "TGE/Services/Service.hpp"
#include "TGE/Services/ServiceLifetime.hpp"
#include "TGE/Services/ServiceTraits.hpp"

namespace TGE
{
    class ServiceLocator;

    /**
     * @class ServiceDescriptor
     * @brief Stores metadata for a registered service entry.
     * @details
     * Descriptors are immutable and safe to share between providers and scopes.
     */
    class TGE_API ServiceDescriptor final
    {
    public:
        using ActivationHandle = std::shared_ptr<void>;
        using ActivationDelegate = ActivationHandle (*)(ServiceLocator&, const ServiceDescriptor&);
        using FactoryDelegate = std::function<ActivationHandle(ServiceLocator&)>;
        using PointerAdapter = void* (*)(void*);

        /**
         * @brief Create a singleton descriptor using the default activator.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Singleton();

        /**
         * @brief Create a scoped descriptor using the default activator.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Scoped();

        /**
         * @brief Create a transient descriptor using the default activator.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Transient();

        /**
         * @brief Create a singleton descriptor backed by an existing instance.
         */
        template<class TService>
            requires IService<TService>
        static ServiceDescriptor Singleton(const std::shared_ptr<TService>& instance);

        /**
         * @brief Create a scoped descriptor backed by an existing instance.
         */
        template<class TService>
            requires IService<TService>
        static ServiceDescriptor Scoped(const std::shared_ptr<TService>& instance);

        /**
         * @brief Create a transient descriptor backed by an existing instance.
         */
        template<class TService>
            requires IService<TService>
        static ServiceDescriptor Transient(const std::shared_ptr<TService>& instance);

        /**
         * @brief Create a singleton descriptor that delegates activation to a user-supplied factory.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Singleton(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Create a scoped descriptor that delegates activation to a user-supplied factory.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Scoped(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Create a transient descriptor that delegates activation to a user-supplied factory.
         */
        template<class TService, class TImplementation = TService>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Transient(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        /**
         * @brief Lifetime policy applied to constructed instances.
         */
        ServiceLifetime GetServiceLifetime() const noexcept { return lifetime; }

        /**
         * @brief Type identifier for the registered service interface.
         */
        std::type_index GetServiceType() const noexcept { return serviceType; }

        /**
         * @brief Type identifier for the implementation used by this descriptor.
         */
        std::type_index GetImplementationType() const noexcept { return implementationType; }

        /**
         * @brief Indicates whether a user-supplied factory overrides the default activator.
         */
        bool HasCustomFactory() const noexcept { return static_cast<bool>(factory); }

        /**
         * @brief Indicates whether an existing instance should be reused for every request.
         */
        bool HasPrebuiltInstance() const noexcept { return static_cast<bool>(existingInstance); }

        /**
         * @brief Creates or retrieves the service instance described by this descriptor.
         */
        ActivationHandle Activate(ServiceLocator& locator) const;

        /**
         * @brief Converts the cached activation result into a strongly typed pointer.
         */
        template<class TService>
        std::shared_ptr<TService> CastToService(const ActivationHandle& instance) const;

    private:
        template<class TService, class TImplementation>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Create(ServiceLifetime lifetime);

        template<class TService>
            requires IService<TService>
        static ServiceDescriptor Create(ServiceLifetime lifetime, const std::shared_ptr<TService>& instance);

        template<class TService, class TImplementation>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static ServiceDescriptor Create(ServiceLifetime lifetime, std::function<std::shared_ptr<TService>(ServiceLocator&)> factory);

        template<class TService, class TImplementation>
            requires IService<TService> && IServiceImplementation<TService, TImplementation>
        static void* AdaptService(void* instance) noexcept;

        template<class TImplementation>
        static void* AdaptImplementation(void* instance) noexcept;

        ServiceDescriptor(ServiceLifetime lifetime,
                          std::type_index serviceType,
                          std::type_index implementationType,
                          ActivationDelegate activator,
                          FactoryDelegate factory,
                          ActivationHandle existingInstance,
                          PointerAdapter serviceAdapter,
                          PointerAdapter implementationAdapter);

        ServiceLifetime lifetime;
        std::type_index serviceType;
        std::type_index implementationType;
        ActivationDelegate activator;
        FactoryDelegate factory;
        ActivationHandle existingInstance;
        PointerAdapter serviceAdapter;
        PointerAdapter implementationAdapter;
    };
}

#include "TGE/Services/ServiceDescriptor.inl"
