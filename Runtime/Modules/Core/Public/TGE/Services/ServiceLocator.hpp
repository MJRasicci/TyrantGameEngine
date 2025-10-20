/**
 * @file ServiceLocator.hpp
 * @brief Base class for resolving services from the dependency injection container.
 */

#pragma once

#include <any>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "TGE/Export.hpp"
#include "TGE/Services/Service.hpp"
#include "TGE/Services/ServiceLifetime.hpp"

namespace TGE
{
    class ServiceDescriptor;
    namespace detail { struct ServiceRegistry; }

    class ServiceProvider;
    class ServiceScope;

    /**
     * @class ServiceLocator
     * @brief Base class that performs service resolution and lifetime management.
     */
    class TGE_API ServiceLocator
    {
    public:
        using ActivationHandle = std::shared_ptr<void>;

        /**
         * @brief Resolve a service or throw if the service is unavailable.
         */
        template<IService TService>
        std::shared_ptr<TService> GetRequiredService();

        /**
         * @brief Resolve a service if available without throwing on failure.
         */
        template<IService TService>
        std::shared_ptr<TService> TryGetService();

    protected:
        ServiceLocator(std::shared_ptr<detail::ServiceRegistry> registry,
                       std::unordered_map<std::type_index, ActivationHandle>* singletonCache,
                       ServiceLocator* root,
                       ServiceLocator* parent);

        virtual ~ServiceLocator() = default;

        const std::shared_ptr<detail::ServiceRegistry>& GetRegistry() const noexcept { return registry; }

    private:

        struct ResolutionResult
        {
            const ServiceDescriptor* descriptor { nullptr };
            ActivationHandle instance;
        };

        ResolutionResult Resolve(std::type_index type, bool required);
        ResolutionResult ResolveInternal(std::type_index type, std::vector<std::type_index>& path, bool required);
        const ServiceDescriptor* FindDescriptor(std::type_index type) const;
        ActivationHandle GetCachedInstance(ServiceLifetime lifetime, std::type_index type) const;
        void CacheInstance(ServiceLifetime lifetime, std::type_index type, const ActivationHandle& instance);

        std::shared_ptr<detail::ServiceRegistry> registry;
        std::unordered_map<std::type_index, ActivationHandle>* singletonCache;
        ServiceLocator* rootLocator;
        ServiceLocator* parentLocator;
        std::unordered_map<std::type_index, ActivationHandle> scopedCache;
        std::vector<std::type_index>* activePath { nullptr };
    };
}

#include "TGE/Services/ServiceLocator.inl"

