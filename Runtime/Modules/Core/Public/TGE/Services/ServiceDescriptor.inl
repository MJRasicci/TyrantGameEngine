#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#if TGE_HAS_REFLECTION_DI
    #include <meta>
#endif

#include "TGE/Services/ServiceLocator.hpp"

namespace TGE
{
    namespace detail
    {
        template<class TService>
        ServiceDescriptor::ActivationHandle WrapExistingInstance(
            const std::shared_ptr<TService>& existingInstance)
        {
            return std::shared_ptr<void>(
                existingInstance, existingInstance.get());
        }

#if TGE_HAS_REFLECTION_DI
        template<class>
        inline constexpr bool AlwaysFalse = false;

        template<class T>
        struct SharedPointerDependency
        {
            static constexpr bool IsSupported = false;
        };

        template<class TService>
        struct SharedPointerDependency<std::shared_ptr<TService>>
        {
            static constexpr bool IsSupported = true;
            using ServiceType = TService;
        };

        /**
         * Select the constructor used by reflection-generated activation.
         *
         * An explicit TGE_INJECT_CONSTRUCTOR annotation takes precedence.
         * Without one, exactly one public non-copy, non-move constructor must
         * be available. Deleted and inaccessible constructors are ignored.
         */
        template<class TImplementation>
        consteval std::meta::info SelectInjectableConstructor()
        {
            std::meta::info candidate {};
            std::meta::info annotated {};
            std::size_t candidateCount = 0;
            std::size_t annotatedCount = 0;

            for (auto member : std::meta::members_of(
                     ^^TImplementation,
                     std::meta::access_context::unprivileged()))
            {
                if (!std::meta::is_constructor(member) ||
                    !std::meta::is_public(member) ||
                    std::meta::is_copy_constructor(member) ||
                    std::meta::is_move_constructor(member) ||
                    std::meta::is_deleted(member))
                {
                    continue;
                }

                candidate = member;
                ++candidateCount;

                if (!std::meta::annotations_of_with_type(
                         member, ^^InjectConstructorAttribute).empty())
                {
                    annotated = member;
                    ++annotatedCount;
                }
            }

            if (annotatedCount == 1)
            {
                return annotated;
            }

            if (annotatedCount == 0 && candidateCount == 1)
            {
                return candidate;
            }

            return {};
        }

        template<class TParameter>
        decltype(auto) ResolveReflectedDependency(ServiceLocator& locator)
        {
            if constexpr (SharedPointerDependency<TParameter>::IsSupported)
            {
                using TService =
                    typename SharedPointerDependency<TParameter>::ServiceType;
                return locator.template GetRequiredService<TService>();
            }
            else if constexpr (std::same_as<TParameter, ServiceLocator&>)
            {
                return (locator);
            }
            else if constexpr (std::same_as<TParameter, ServiceLocator*>)
            {
                return &locator;
            }
            else
            {
                static_assert(
                    AlwaysFalse<TParameter>,
                    "Reflection-generated service activation supports "
                    "std::shared_ptr<T>, ServiceLocator&, and ServiceLocator* "
                    "constructor parameters.");
            }
        }

        template<class TService,
                 class TImplementation,
                 std::meta::info TConstructor,
                 std::size_t... Indices>
        ServiceDescriptor::ActivationHandle InstantiateFromReflection(
            ServiceLocator& locator,
            std::index_sequence<Indices...>)
        {
            auto instance = std::make_shared<TImplementation>(
                ResolveReflectedDependency<
                    typename [:std::meta::type_of(
                        std::meta::parameters_of(TConstructor)[Indices]):]
                >(locator)...);

            return std::shared_ptr<void>(
                instance, static_cast<void*>(instance.get()));
        }

        template<class TService, class TImplementation>
        ServiceDescriptor::ActivationHandle ActivateUsingReflection(
            ServiceLocator& locator,
            const ServiceDescriptor&)
        {
            constexpr auto constructor =
                SelectInjectableConstructor<TImplementation>();

            if constexpr (constructor == std::meta::info {})
            {
                static_assert(
                    AlwaysFalse<TImplementation>,
                    "A reflected service implementation must expose exactly "
                    "one public non-copy, non-move constructor, or mark "
                    "exactly one constructor with "
                    "TGE_INJECT_CONSTRUCTOR.");
                return {};
            }
            else
            {
                constexpr std::size_t dependencyCount =
                    std::meta::parameters_of(constructor).size();

                return InstantiateFromReflection<
                    TService,
                    TImplementation,
                    constructor>(
                        locator,
                        std::make_index_sequence<dependencyCount> {});
            }
        }
#else
        template<class TTag>
        auto ResolveDependency(ServiceLocator& locator, TTag tag)
        {
            if constexpr (std::is_same_v<TTag, LocatorDependency>)
            {
                return std::ref(locator);
            }
            else
            {
                return locator.template GetRequiredService<typename TTag::ServiceType>();
            }
        }

        template<class TService, class TImplementation, class Tuple, std::size_t... Indices>
        ServiceDescriptor::ActivationHandle InstantiateFromTuple(
            ServiceLocator& locator, const Tuple& tuple, std::index_sequence<Indices...>)
        {
            auto instance = std::make_shared<TImplementation>(
                ResolveDependency(locator, std::get<Indices>(tuple))...);
            return std::shared_ptr<void>(
                instance, static_cast<void*>(instance.get()));
        }

        template<class TService, class TImplementation>
        ServiceDescriptor::ActivationHandle ActivateUsingTraits(ServiceLocator& locator, const ServiceDescriptor&)
        {
            auto dependencies = ServiceDependencyTraits<TImplementation>::Dependencies();
            constexpr std::size_t dependencyCount = std::tuple_size_v<decltype(dependencies)>;
            return InstantiateFromTuple<TService, TImplementation>(
                locator, dependencies, std::make_index_sequence<dependencyCount> {});
        }
#endif
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Singleton()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Singleton);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Scoped()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Scoped);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Transient()
    {
        return Create<TService, TImplementation>(ServiceLifetime::Transient);
    }

    template<class TService>
        requires IService<TService>
    ServiceDescriptor ServiceDescriptor::Singleton(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Singleton, instance);
    }

    template<class TService>
        requires IService<TService>
    ServiceDescriptor ServiceDescriptor::Scoped(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Scoped, instance);
    }

    template<class TService>
        requires IService<TService>
    ServiceDescriptor ServiceDescriptor::Transient(const std::shared_ptr<TService>& instance)
    {
        return Create(ServiceLifetime::Transient, instance);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Singleton(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Singleton, std::move(factory));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Scoped(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Scoped, std::move(factory));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Transient(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        return Create<TService, TImplementation>(ServiceLifetime::Transient, std::move(factory));
    }

    inline ServiceDescriptor::ActivationHandle ServiceDescriptor::Activate(ServiceLocator& locator) const
    {
        if (existingInstance)
        {
            return existingInstance;
        }

        if (factory)
        {
            auto result = factory(locator);

            if (!result)
            {
                throw std::domain_error(std::format(
                    "Factory for service \"{}\" returned a null pointer.", serviceType.name()));
            }

            return result;
        }

        if (!activator)
        {
            throw std::domain_error(std::format(
                "Descriptor for service \"{}\" does not provide an activator.", serviceType.name()));
        }

        return activator(locator, *this);
    }

    template<class TService>
    std::shared_ptr<TService> ServiceDescriptor::CastToService(const ActivationHandle& instance) const
    {
        if (!instance)
        {
            return nullptr;
        }

        const std::type_index requested = typeid(TService);

        if (requested != serviceType && requested != implementationType)
        {
            throw std::domain_error(std::format(
                "Cannot cast service registration \"{}\" to requested type \"{}\".",
                serviceType.name(), requested.name()));
        }

        void* adjusted = requested == serviceType
            ? serviceAdapter(instance.get())
            : implementationAdapter(instance.get());

        return std::shared_ptr<TService>(instance, static_cast<TService*>(adjusted));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Create(ServiceLifetime lifetime)
    {
        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TImplementation),
#if TGE_HAS_REFLECTION_DI
            &detail::ActivateUsingReflection<TService, TImplementation>,
#else
            &detail::ActivateUsingTraits<TService, TImplementation>,
#endif
            {},
            {},
            &ServiceDescriptor::AdaptService<TService, TImplementation>,
            &ServiceDescriptor::AdaptImplementation<TImplementation>);
    }

    template<class TService>
        requires IService<TService>
    ServiceDescriptor ServiceDescriptor::Create(ServiceLifetime lifetime, const std::shared_ptr<TService>& instance)
    {
        if (!instance)
        {
            throw std::domain_error(std::format(
                "Attempted to register a null existing instance for service \"{}\".",
                typeid(TService).name()));
        }

        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TService),
            nullptr,
            {},
            detail::WrapExistingInstance(instance),
            &ServiceDescriptor::AdaptService<TService, TService>,
            &ServiceDescriptor::AdaptImplementation<TService>);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    ServiceDescriptor ServiceDescriptor::Create(ServiceLifetime lifetime, std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        if (!factory)
        {
            throw std::domain_error(std::format(
                "Factory registration for service \"{}\" cannot be null.",
                typeid(TService).name()));
        }

        FactoryDelegate wrapped = [inner = std::move(factory)](ServiceLocator& locator)
        {
            auto produced = inner(locator);

            if (!produced)
            {
                throw std::domain_error("Service factory returned a null instance.");
            }

            return detail::WrapExistingInstance(produced);
        };

        return ServiceDescriptor(
            lifetime,
            typeid(TService),
            typeid(TImplementation),
            nullptr,
            std::move(wrapped),
            {},
            &ServiceDescriptor::AdaptService<TService, TImplementation>,
            &ServiceDescriptor::AdaptImplementation<TImplementation>);
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    void* ServiceDescriptor::AdaptService(void* instance) noexcept
    {
        auto implementation = static_cast<TImplementation*>(instance);

        if constexpr (std::is_same_v<TService, TImplementation>)
        {
            return implementation;
        }
        else
        {
            return static_cast<TService*>(implementation);
        }
    }

    template<class TImplementation>
    void* ServiceDescriptor::AdaptImplementation(void* instance) noexcept
    {
        return static_cast<TImplementation*>(instance);
    }

    inline ServiceDescriptor::ServiceDescriptor(ServiceLifetime lifetime,
                                               std::type_index serviceType,
                                               std::type_index implementationType,
                                               ActivationDelegate activator,
                                               FactoryDelegate factory,
                                               ActivationHandle existingInstance,
                                               PointerAdapter serviceAdapter,
                                               PointerAdapter implementationAdapter)
        : lifetime(lifetime),
          serviceType(serviceType),
          implementationType(implementationType),
          activator(activator),
          factory(std::move(factory)),
          existingInstance(existingInstance),
          serviceAdapter(serviceAdapter),
          implementationAdapter(implementationAdapter)
    {
    }
}
