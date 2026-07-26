/**
 * @file ServiceTraits.hpp
 * @brief Constructor-injection metadata shared by reflected and portable builds.
 *
 * Reflection-enabled builds use the constructor annotation declared here.
 * Portable builds use dependency-trait specializations to describe the
 * constructor arguments resolved by the service container.
 */

#pragma once

#include <concepts>
#include <memory>
#include <tuple>
#include <type_traits>

#ifndef TGE_HAS_REFLECTION_DI
    #define TGE_HAS_REFLECTION_DI 0
#endif

#if TGE_HAS_REFLECTION_DI
    #if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202603L
        #error "TGE reflection DI requires C++26 reflection support enabled with -freflection."
    #endif
#endif

namespace TGE
{
    class ServiceLocator;

    /**
     * @struct InjectConstructorAttribute
     * @brief Annotation value type used to select an injectable constructor.
     * @details
     * A reflected implementation with multiple public constructors must mark
     * exactly one constructor with TGE_INJECT_CONSTRUCTOR. Implementations with
     * a single public, non-copy, non-move constructor need no annotation.
     */
    struct InjectConstructorAttribute final
    {
    };

    /**
     * @brief Annotation value reflected from TGE_INJECT_CONSTRUCTOR.
     */
    inline constexpr InjectConstructorAttribute InjectConstructor {};

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

/**
 * @brief Select a constructor for C++26 reflection-generated activation.
 * @details
 * The macro expands to a C++26 annotation when reflection DI is enabled and
 * to nothing for the portable traits-based fallback.
 */
#if TGE_HAS_REFLECTION_DI
    #define TGE_INJECT_CONSTRUCTOR [[=TGE::InjectConstructor]]
#else
    #define TGE_INJECT_CONSTRUCTOR
#endif
