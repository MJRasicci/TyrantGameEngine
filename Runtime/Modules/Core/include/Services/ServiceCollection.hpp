/**
 * @file ServiceCollection.hpp
 * @brief Defines the ServiceCollection class for the Tyrant Game Engine dependency injection system.
 *
 * The ServiceCollection class provides mechanisms for registering services along with
 * their lifetimes and implementations. It acts as a registry for service descriptors,
 * enabling the creation of a ServiceProvider with all registered services.
 */

#pragma once

#include "Export.hpp"
#include "Service.hpp"
#include "ServiceDescriptor.hpp"

namespace TGE {
    
//===========================================================================//
//=======> ServiceProvider class <===========================================//
//===========================================================================//

class ServiceProvider;

/**
 * @class ServiceCollection
 * @brief Manages the registration of services and their descriptors in the Tyrant Game Engine system.
 *
 * ServiceCollection allows for the registration of services with associated lifetimes and
 * implementations. It maintains a collection of ServiceDescriptors, each defining how a
 * particular service should be instantiated and managed within the Tyrant Game Engine container.
 */
class TGE_API ServiceCollection final
{
public:
    /**
     * @brief Registers a service with a Singleton lifetime.
     * @tparam TService The service type to register.
     * @tparam TImplementation The implementation type of the service, defaulting to TService.
     */
    template<IService TService, IServiceImplementation<TService> TImplementation = TService>
    void AddSingleton()
    {
        Add(ServiceDescriptor::Singleton<TService, TImplementation>());
    }

    /**
     * @brief Registers a service with a Scoped lifetime.
     * @tparam TService The service type to register.
     * @tparam TImplementation The implementation type of the service, defaulting to TService.
     */
    template<IService TService, IServiceImplementation<TService> TImplementation = TService>
    void AddScoped()
    {
        Add(ServiceDescriptor::Scoped<TService, TImplementation>());
    }

    /**
     * @brief Registers a service with a Transient lifetime.
     * @tparam TService The service type to register.
     * @tparam TImplementation The implementation type of the service, defaulting to TService.
     */
    template<IService TService, IServiceImplementation<TService> TImplementation = TService>
    void AddTransient()
    {
        Add(ServiceDescriptor::Transient<TService, TImplementation>());
    }

    /**
     * @brief Retrieves the ServiceDescriptor for a specified service type.
     * @tparam TService The service type for which to retrieve the descriptor.
     * @return ServiceDescriptor The descriptor associated with TService.
     */
    template<IService TService>
    ServiceDescriptor GetDescriptorFor() const
    {
        const std::type_index serviceTypeId = typeid(TService);
        return GetDescriptorFor(serviceTypeId);
    }

    /**
     * @brief Retrieves the ServiceDescriptor for a given type index.
     * @param serviceTypeId The type index of the service.
     * @return ServiceDescriptor The descriptor for the specified service type.
     */
    ServiceDescriptor GetDescriptorFor(std::type_index serviceTypeId) const;

    /**
     * @brief Returns a list of all registered service descriptors.
     * @return std::vector<ServiceDescriptor> A vector containing all registered service descriptors.
     */
    std::vector<ServiceDescriptor> GetRegisteredServices() const;

    /**
     * @brief Builds a ServiceProvider with all registered services.
     * @return std::shared_ptr<ServiceProvider> A shared pointer to the ServiceProvider.
     */
    std::shared_ptr<ServiceProvider> BuildServiceProvider() const;

private:
    /**
     * @brief A map holding registered services keyed by their type index.
     *
     * This unordered map stores ServiceDescriptors, allowing quick retrieval and management
     * of service registrations based on their type index.
     */
    std::unordered_map<std::type_index, ServiceDescriptor> registeredServices;

    /**
     * @brief A map associating service types with their implementation types.
     *
     * This unordered map is used to maintain relationships between service interfaces and
     * their respective implementation types, facilitating type resolution and casting.
     */
    std::unordered_map<std::type_index, std::type_index> serviceImplementations;

    /**
     * @brief Adds a ServiceDescriptor to the collection.
     * @param descriptor The ServiceDescriptor to add to the collection.
     */
    void Add(ServiceDescriptor descriptor);
};

}
