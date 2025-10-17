/**
 * @file ServiceProvider.hpp
 * @brief Defines the root provider and child scopes for service resolution.
 */

#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>

#include "Export.hpp"
#include "Services/ServiceLocator.hpp"

namespace TGE
{
    /**
     * @class ServiceProvider
     * @brief Root service locator that owns singleton instances.
     */
    class TGE_API ServiceProvider : public ServiceLocator, public std::enable_shared_from_this<ServiceProvider>
    {
    public:
        /**
         * @brief Create a new scope that inherits singleton registrations.
         */
        std::shared_ptr<ServiceScope> CreateScope();

    protected:
        using ActivationHandle = ServiceLocator::ActivationHandle;

        explicit ServiceProvider(std::shared_ptr<detail::ServiceRegistry> registry);

    private:
        std::unordered_map<std::type_index, ActivationHandle> singletonStorage;

        friend class ServiceCollection;
        friend class ServiceScope;
    };

    /**
     * @class ServiceScope
     * @brief Provides scoped lifetime semantics for services.
     */
    class TGE_API ServiceScope : public ServiceLocator
    {
    public:
        ~ServiceScope() override = default;

    private:
        using ActivationHandle = ServiceLocator::ActivationHandle;

        explicit ServiceScope(std::shared_ptr<ServiceProvider> rootProvider);

        std::shared_ptr<ServiceProvider> rootOwner;

        friend class ServiceProvider;
    };
}

