/**
 * @file ServiceDescriptor.hpp
 * @brief Descriptor and factory metadata for services registered in Tyrant Game Engine.
 * @details
 * `ServiceDescriptor` captures:
 *  - The service interface type and concrete implementation type.
 *  - The lifetime policy (`Singleton`, `Scoped`, `Transient`).
 *  - The construction strategy (default factory, locator-aware factory, or custom factory).
 *  - An optional downcast adapter to convert stored interface pointers to concrete or sibling interfaces.
 *
 * Typical usage:
 * @code
 * // Register IFoo -> Foo as a singleton
 * auto d = TGE::ServiceDescriptor::Singleton<IFoo, Foo>();
 *
 * // Register with custom factory
 * auto d2 = TGE::ServiceDescriptor::Transient<IFoo, Foo>(
 *   [](TGE::ServiceLocator* sl){ return std::make_shared<std::any>(std::make_shared<Foo>(sl)); }
 * );
 * @endcode
 */

#pragma once

#include <functional>
#include <stdexcept>
#include <format>
#include <typeindex>
#include <any>
#include <memory>
#include "Export.hpp"
#include "Services/Service.hpp"
#include "Services/ServiceLifetime.hpp"

namespace TGE {

/**
 * @class ServiceDescriptor
 * @brief Immutable description of a registered service.
 * @details
 * A descriptor binds a service interface `TService` to an implementation `TImplementation`.
 * It owns factory functions that produce `std::shared_ptr<TService>` wrapped in `std::any`
 * for type-erased storage inside the container. Descriptors are value types and can be
 * copied freely; they hold callable objects and lightweight identifiers (`std::type_index`).
 *
 * Thread-safety: The descriptor is immutable after construction and is therefore
 * safe to share across threads. The thread-safety of constructed service instances
 * depends on the implementation type and lifetime policy.
 */
class TGE_API ServiceDescriptor final
{
public:
    /**
     * @brief Create a descriptor with `Singleton` lifetime.
     * @tparam TService Service interface type. Must satisfy `IService`.
     * @tparam TImplementation Concrete type. Must satisfy `IServiceImplementation<TService, TImplementation>`.
     * @param factoryMethod Optional factory that returns `std::shared_ptr<std::any>` holding `std::shared_ptr<TService>`.
     *                      If null, a default factory is synthesized. The synthesized factory prefers a
     *                      `TImplementation(ServiceLocator*)` constructor when available, otherwise the default constructor.
     * @return A descriptor configured for singleton semantics.
     * @note Use `nullptr` for no custom factory. Passing a non-null factory replaces the synthesized one.
     * @warning The provided factory must return an `std::any` that contains `std::shared_ptr<TService>`.
     *          Violating this precondition will cause `CastToService` to throw at runtime.
     */
    template<IService TService, IServiceImplementation<TService> TImplementation>
    static ServiceDescriptor Singleton(std::function<std::shared_ptr<std::any>(ServiceLocator*)> factoryMethod = NULL)
    {
        return createDescriptorFor<TService, TImplementation>(ServiceLifetime::Singleton, factoryMethod);
    }

    /**
     * @brief Create a descriptor with `Scoped` lifetime.
     * @tparam TService Service interface type. Must satisfy `IService`.
     * @tparam TImplementation Concrete type. Must satisfy `IServiceImplementation<TService, TImplementation>`.
     * @param factoryMethod Optional custom factory. See @ref Singleton for semantics and constraints.
     * @return A descriptor configured for scoped semantics.
     */
    template<IService TService, IServiceImplementation<TService> TImplementation>
    static ServiceDescriptor Scoped(std::function<std::shared_ptr<std::any>(ServiceLocator*)> factoryMethod = NULL)
    {
        return createDescriptorFor<TService, TImplementation>(ServiceLifetime::Scoped, factoryMethod);
    }

    /**
     * @brief Create a descriptor with `Transient` lifetime.
     * @tparam TService Service interface type. Must satisfy `IService`.
     * @tparam TImplementation Concrete type. Must satisfy `IServiceImplementation<TService, TImplementation>`.
     * @param factoryMethod Optional custom factory. See @ref Singleton for semantics and constraints.
     * @return A descriptor configured for transient semantics.
     */
    template<IService TService, IServiceImplementation<TService> TImplementation>
    static ServiceDescriptor Transient(std::function<std::shared_ptr<std::any>(ServiceLocator*)> factoryMethod = NULL)
    {
        return createDescriptorFor<TService, TImplementation>(ServiceLifetime::Transient, factoryMethod);
    }

    /**
     * @brief Lifetime policy for instances produced by this descriptor.
     * @return The configured `ServiceLifetime`.
     */
    ServiceLifetime GetServiceLifetime() const { return lifetime; }

    /**
     * @brief Identifier of the logical service interface.
     * @return `std::type_index` for `TService`.
     * @details
     * Used by the container to match requested services to descriptors at runtime.
     */
    std::type_index GetServiceType() const { return serviceType; }

    /**
     * @brief Identifier of the concrete implementation type.
     * @return `std::type_index` for `TImplementation`.
     */
    std::type_index GetImplementationType() const { return implementationType; }

    /**
     * @brief Invoke the factory to build an instance.
     * @param locator A non-owning pointer to the `ServiceLocator` for dependency resolution.
     * @return `std::shared_ptr<std::any>` containing `std::shared_ptr<TService>`.
     * @pre `factoryMethod` is non-null.
     * @note The container unwraps and manages the `shared_ptr<TService>`; the outer `shared_ptr<any>`
     *       is an implementation detail for type-erased transport.
     */
    std::shared_ptr<std::any> BuildInstance(ServiceLocator* locator) const { return factoryMethod(locator); }

    /**
     * @brief Extract a strongly-typed service pointer from a type-erased reference.
     * @tparam TService Target service type to cast to.
     * @param serviceReference A `shared_ptr<any>` produced by @ref BuildInstance.
     * @return `std::shared_ptr<TService>` on success.
     * @throws std::domain_error If the requested type does not match the descriptor's types or
     *                           if the contained value cannot be cast to `std::shared_ptr<TService>`.
     * @details
     * If `TService` equals the descriptor's `serviceType`, the method directly `any_cast`s the stored pointer.
     * If `TService` equals the `implementationType` and differs from `serviceType`, the method uses
     * `downcastFunction` to adapt the stored pointer before casting.
     */
    template<IService TService>
    std::shared_ptr<TService> CastToService(std::shared_ptr<std::any> serviceReference) const
    {
        const std::type_index& outType = typeid(TService);

        if (outType != serviceType && outType != implementationType)
            throw std::domain_error(std::format(
                "Failed to cast service reference: Output type \"{0}\" is not valid for this descriptor. "
                "Valid options are \"{1}\" or \"{2}\"",
                outType.name(), serviceType.name(), implementationType.name()));

        try
        {
            if (serviceType == implementationType || outType == serviceType)
                return std::any_cast<std::shared_ptr<TService>>(*serviceReference);
            else
                return std::any_cast<std::shared_ptr<TService>>(*downcastFunction(serviceReference));
        }
        catch (std::bad_any_cast ex)
        {
            throw std::domain_error(std::format(
                "{0}: Failed to cast AnyReference to ServiceReference<{1}>",
                ex.what(), outType.name()));
        }
    }

private:
    /**
     * @brief Lifetime policy selected for this descriptor.
     * @see ServiceLifetime
     */
    ServiceLifetime lifetime;

    /**
     * @brief Type identity of the service interface bound by this descriptor.
     * @details
     * Used for lookup and validation during resolution and casts.
     */
    std::type_index serviceType;

    /**
     * @brief Type identity of the concrete implementation bound by this descriptor.
     * @details
     * Must represent a type that publicly derives from the service interface.
     */
    std::type_index implementationType;

    /**
     * @brief Primary factory used to construct instances.
     * @details
     * Must return an `std::any` that contains a `std::shared_ptr<TService>`.
     * The `ServiceLocator*` argument enables locator-aware construction paths.
     */
    std::function<std::shared_ptr<std::any>(ServiceLocator*)> factoryMethod;

    /**
     * @brief Adapter used to convert a stored `shared_ptr<TService>` to another service-compatible type.
     * @details
     * Default implementation performs `static_pointer_cast<TImplementation>` when the requested
     * output type equals `implementationType`. Containers may rely on this when users
     * request either the interface or the concrete implementation.
     */
    std::function<std::shared_ptr<std::any>(std::shared_ptr<std::any>)> downcastFunction;

    /**
     * @brief Construct a fully-initialized descriptor.
     * @param lifetime Lifetime policy.
     * @param serviceType Type id of the service interface.
     * @param implementationType Type id of the concrete implementation.
     * @param factoryMethod Factory that builds instances (see @ref BuildInstance).
     * @param downcastFunction Adapter for implementation-directed casts (see @ref CastToService).
     * @pre `factoryMethod` and `downcastFunction` are non-null.
     */
    ServiceDescriptor(ServiceLifetime lifetime,
                      std::type_index serviceType,
                      std::type_index implementationType,
                      std::function<std::shared_ptr<std::any>(ServiceLocator*)> factoryMethod,
                      std::function<std::shared_ptr<std::any>(std::shared_ptr<std::any>)> downcastFunction)
        : lifetime(lifetime),
          serviceType(serviceType),
          implementationType(implementationType),
          factoryMethod(factoryMethod),
          downcastFunction(downcastFunction)
    { }

    /**
     * @brief Synthesize a descriptor for the given service pair and lifetime.
     * @tparam TService Service interface type.
     * @tparam TImplementation Concrete implementation type.
     * @param lifetime Lifetime policy to apply.
     * @param factoryMethod Optional custom factory. If null, a synthesized factory is used that:
     *   - Preferentially invokes `TImplementation(ServiceLocator*)` when the concept
     *     `IConstructableFromLocator<TImplementation>` is satisfied.
     *   - Falls back to the default constructor otherwise.
     * @return A fully-initialized descriptor.
     * @note The synthesized factory always stores `std::shared_ptr<TService>` inside `std::any`.
     * @warning If a custom factory returns an incompatible `any` payload, subsequent casts will fail.
     */
    template<IService TService, IServiceImplementation<TService> TImplementation>
    static ServiceDescriptor createDescriptorFor(ServiceLifetime lifetime, std::function<std::shared_ptr<std::any>(ServiceLocator*)> factoryMethod)
    {
        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TImplementation),
            factoryMethod ? factoryMethod
                          : [](ServiceLocator* locator) -> std::shared_ptr<std::any>
            {
                if constexpr (IConstructableFromLocator<TImplementation>)
                {
                    // Prefer constructor that accepts a ServiceLocator*
                    return std::make_shared<std::any>(
                        std::shared_ptr<TService>(new TImplementation(locator)));
                }
                else
                {
                    // Fallback to default construction
                    return std::make_shared<std::any>(
                        std::shared_ptr<TService>(new TImplementation()));
                }
            },
            [](std::shared_ptr<std::any> base) -> std::shared_ptr<std::any>
            {
                return std::make_shared<std::any>(
                    std::static_pointer_cast<TImplementation>(
                        std::any_cast<std::shared_ptr<TService>>(*base)));
            }
        );
    }
};

} // namespace TGE
