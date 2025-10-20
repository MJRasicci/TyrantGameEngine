/**
 * @file ServiceLifetime.hpp
 * @brief Defines lifetime policies for services in the Tyrant Game Engine container.
 * @details
 * The `ServiceLifetime` enumeration specifies how services are instantiated,
 * cached, and disposed within the dependency injection system.
 *
 * Lifetime policies:
 *  - `Singleton`: A single instance is created once and reused throughout the process.
 *  - `Scoped`: A new instance is created per logical scope (e.g. per request, per frame, or per custom container scope).
 *  - `Transient`: A new instance is created every time the service is requested.
 *
 * This choice affects performance, memory usage, and sharing semantics across the engine.
 */

#pragma once

#include "TGE/Export.hpp"

namespace TGE {

//===========================================================================//
//=======> ServiceLifetime Enumeration <=====================================//
//===========================================================================//

/**
 * @enum ServiceLifetime
 * @brief Defines supported service lifetime policies.
 *
 * Determines how often the container creates instances for a given service.
 * Used internally by @ref ServiceDescriptor and @ref ServiceLocator to enforce
 * service caching and reuse rules.
 *
 * @note These semantics mirror common DI frameworks in .NET and other environments,
 *       allowing familiarity for developers with prior DI experience.
 */
enum TGE_API ServiceLifetime
{
    /**
     * @brief A single instance for the application's lifetime.
     * @details
     * - Created once on first resolution.
     * - Reused for all subsequent requests.
     * - Suitable for stateless services, managers, and shared resources.
     * - Avoid if the service holds request-specific or thread-specific state.
     */
    Singleton,

    /**
     * @brief A unique instance per defined scope.
     * @details
     * - A new instance is created for each container scope.
     * - Within that scope, the instance is reused.
     * - Common in web servers (per request) or game engines (per frame or subsystem scope).
     * - Prevents cross-scope state bleed while allowing reuse inside a scope.
     */
    Scoped,

    /**
     * @brief Always a fresh instance.
     * @details
     * - A new instance is returned on every resolution.
     * - Useful for lightweight, short-lived, or disposable services.
     * - Higher instantiation cost; avoid for expensive-to-construct services.
     * - Caller is responsible for managing lifecycle beyond resolution.
     */
    Transient
};

} // namespace TGE
