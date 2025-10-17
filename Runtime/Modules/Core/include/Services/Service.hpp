/**
 * @file Service.hpp
 * @brief Concepts for registering and resolving services in Tyrant Game Engine.
 * @details
 * Defines the minimal construction requirements for a type to qualify as a *service*
 * and validates the relationship between a service interface and its implementation.
 *
 * Concepts:
 *  - `IConstructable<T>`: default-constructible services.
 *  - `IConstructableFromLocator<T>`: services constructible from `ServiceLocator*`.
 *  - `IService<T>`: a service satisfies either construction path.
 *  - `IServiceImplementation<TService, TImplementation>`: implementation derives from service,
 *    and both satisfy `IService`.
 *
 * @see TGE::ServiceLocator
 */

#pragma once

#include <concepts>
#include <type_traits>

#include "Services/ServiceTraits.hpp"

namespace TGE {

/**
 * @brief Forward declaration for the engine's service locator.
 * @details
 * `ServiceLocator` supplies dependencies to services that opt into
 * locator-based construction. The concrete type is defined elsewhere.
 */
class ServiceLocator;

// ==========================================================================
// IConstructable
// ==========================================================================

/**
 * @concept IConstructable
 * @brief Requires default construction of a service type.
 * @tparam TService Candidate service type.
 * @details
 * Requirements:
 * - `TService` has an accessible default constructor that returns `TService`.
 * @code
 * struct Foo { Foo() = default; };
 * static_assert(TGE::IConstructable<Foo>);
 * @endcode
 */
template<class TService>
concept IConstructable = requires
{
    { TService() } -> std::same_as<TService>;
};

// ==========================================================================
// IConstructableFromLocator
// ==========================================================================

/**
 * @concept IConstructableFromLocator
 * @brief Requires construction of a service from a `ServiceLocator*`.
 * @tparam TService Candidate service type.
 * @details
 * Requirements:
 * - `TService` has a constructor `TService(ServiceLocator*)` that returns `TService`.
 * @code
 * struct Bar {
 *   explicit Bar(TGE::ServiceLocator*) {}
 * };
 * static_assert(TGE::IConstructableFromLocator<Bar>);
 * @endcode
 */
template<class TService>
concept IConstructableFromLocator = requires (ServiceLocator* locator)
{
    { TService(locator) } -> std::same_as<TService>;
};

// ==========================================================================
// IService
// ==========================================================================

/**
 * @concept IService
 * @brief A valid engine service.
 * @tparam TService Candidate service type.
 * @details
 * A type is a service if it can be default-constructed **or** constructed
 * from `ServiceLocator*`. Either path is sufficient.
 * @note Tighten this concept later if additional invariants emerge.
 */
template<class TService>
concept IService = IConstructable<TService> || IConstructableFromLocator<TService> || ServiceConstructionTraits<TService>;

// ==========================================================================
// IServiceImplementation
// ==========================================================================

/**
 * @concept IServiceImplementation
 * @brief Validates the relationship between a service interface and its implementation.
 * @tparam TService Abstract or base service type (interface).
 * @tparam TImplementation Concrete implementation type.
 * @details
 * Requirements:
 *  - `TService` satisfies `IService`.
 *  - `TImplementation` satisfies `IService`.
 *  - `TImplementation` publicly derives from `TService`.
 *
 * @code
 * struct IFoo { virtual ~IFoo() = default; };
 * struct Foo : IFoo { Foo() = default; };
 * static_assert(TGE::IService<IFoo> == false); // not constructible
 * static_assert(TGE::IService<Foo>);           // default-constructible
 * // If IFoo had a locator constructor, then:
 * // static_assert(TGE::IService<IFoo>);
 * // static_assert(TGE::IServiceImplementation<IFoo, Foo>);
 * @endcode
 */
template<class TService, class TImplementation>
concept IServiceImplementation =
    IService<TService> &&
    IService<TImplementation> &&
    std::is_base_of<TService, TImplementation>::value; // Implementation derives from Service

} // namespace TGE
