/**
 * @file ServiceRegistry.hpp
 * @brief Internal storage for registered service descriptors.
 */

#pragma once

#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "TGE/Application/IHostedService.hpp"
#include "TGE/Services/ServiceDescriptor.hpp"

namespace TGE
{
    class ServiceLocator;
}

namespace TGE::detail
{
    /**
     * @struct ServiceRegistry
     * @brief Aggregates descriptors and implementation lookups for a collection.
     */
    struct ServiceRegistry
    {
        /**
         * @brief Map of service type to descriptor metadata.
         */
        std::unordered_map<std::type_index, ServiceDescriptor> descriptors;

        /**
         * @brief Maps implementation type back to the canonical service type.
         */
        std::unordered_map<std::type_index, std::type_index> implementationLookup;

        /**
         * @brief Resolvers for application-managed services in start order.
         */
        std::vector<std::function<std::shared_ptr<IHostedService>(ServiceLocator&)>>
            hostedServiceFactories;
    };
}
