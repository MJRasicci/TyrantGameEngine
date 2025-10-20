/**
 * @file ServiceTraits.hpp
 * @brief Compile-time traits that describe service constructor dependencies.
 *
 * The dependency injection container uses the dependency tuple declared for a
 * service implementation to resolve the constructor arguments required to
 * instantiate the service. A default, empty dependency list is supplied for
 * services that require no additional arguments.
 */

#pragma once

#include <concepts>
#include <memory>
#include <tuple>
#include <type_traits>

namespace TGE
{
    class ServiceLocator;

    namespace detail
    {
        template<class T>
        concept TupleLike = requires
        {
            typename std::tuple_size<std::remove_reference_t<T>>::type;
        };
    }

    /**
     * @brief Primary traits template used to describe constructor dependencies.
     * @tparam T Service implementation declaring its dependencies.
     * @details
     * Specializations should return a tuple composed of dependency tags created
     * with helpers such as ::TGE::Inject and ::TGE::InjectLocator.
     */
    template<class T>
    struct ServiceDependencyTraits
    {
        /**
         * @brief Dependency tuple used to construct @p T.
         * @return Tuple of dependency descriptors evaluated at compile time.
         */
        static constexpr auto Dependencies() noexcept
        {
            return std::tuple<> {};
        }
    };

    /**
     * @concept ServiceConstructionTraits
     * @brief Constrains dependency trait specializations.
     * @tparam T Service implementation whose dependencies are described.
     */
    template<class T>
    concept ServiceConstructionTraits = requires
    {
        { ServiceDependencyTraits<T>::Dependencies() } -> detail::TupleLike;
    };

    /**
     * @struct ServiceDependency
     * @brief Describes a dependency resolved through the locator.
     * @tparam TService Service type that must be resolved.
     */
    template<class TService>
    struct ServiceDependency
    {
        using ServiceType = TService;

        using ResultType = std::shared_ptr<TService>;
    };

    /**
     * @struct LocatorDependency
     * @brief Marker for constructors that request a locator reference.
     */
    struct LocatorDependency
    {
        using ResultType = ServiceLocator&;
    };

    /**
     * @brief Helper that declares a service dependency entry.
     * @tparam TService Service interface requested by the constructor.
     * @return ServiceDependency describing the injected service.
     */
    template<class TService>
    constexpr auto Inject() noexcept
    {
        return ServiceDependency<TService> {};
    }

    /**
     * @brief Helper for constructors that require direct locator access.
     * @return Descriptor representing the locator dependency.
     */
    inline constexpr LocatorDependency InjectLocator() noexcept
    {
        return LocatorDependency {};
    }

    /**
     * @brief Convenience macro for specializing ::TGE::ServiceDependencyTraits.
     */
    #define TGE_DECLARE_SERVICE_DEPENDENCIES(Type, ...) \
        template<> \
        struct TGE::ServiceDependencyTraits<Type> \
        { \
            static constexpr auto Dependencies() noexcept \
            { \
                return std::make_tuple(__VA_ARGS__); \
            } \
        };
}

