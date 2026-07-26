#include <gtest/gtest.h>

#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceLocator.hpp"
#include "TGE/Services/ServiceProvider.hpp"
#include "TGE/Services/ServiceTraits.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    struct SingletonService
    {
        SingletonService()
            : id(nextId++)
        {
        }

        int id;
        static inline int nextId = 1;
    };

    struct ScopedService
    {
        ScopedService()
            : id(nextId++)
        {
        }

        int id;
        static inline int nextId = 1;
    };

    struct TransientService
    {
        TransientService()
            : id(nextId++)
        {
        }

        int id;
        static inline int nextId = 1;
    };

    struct DependentService
    {
        explicit DependentService(std::shared_ptr<SingletonService> singleton)
            : dependency(std::move(singleton))
        {
        }

        std::shared_ptr<SingletonService> dependency;
    };

    struct LocatorAwareService
    {
        explicit LocatorAwareService(TGE::ServiceLocator& locator)
            : locatorAddress(&locator)
        {
        }

        TGE::ServiceLocator* locatorAddress { nullptr };
    };

    struct CyclicA;
    struct CyclicB;

    struct CyclicA
    {
        explicit CyclicA(std::shared_ptr<CyclicB> b)
            : dependency(std::move(b))
        {
        }

        std::shared_ptr<CyclicB> dependency;
    };

    struct CyclicB
    {
        explicit CyclicB(std::shared_ptr<CyclicA> a)
            : dependency(std::move(a))
        {
        }

        std::shared_ptr<CyclicA> dependency;
    };

    struct FactoryService
    {
        FactoryService() = default;

        explicit FactoryService(std::string value)
            : payload(std::move(value))
        {
        }

        std::string payload;
    };

    struct OffsetBase
    {
        virtual ~OffsetBase() = default;

        int padding = 17;
    };

    struct IOffsetService
    {
        virtual ~IOffsetService() = default;
        virtual int Value() const = 0;
    };

    struct OffsetService final : OffsetBase, IOffsetService
    {
        int Value() const override
        {
            return padding;
        }
    };

#if TGE_HAS_REFLECTION_DI
    struct IReflectedDependency
    {
        virtual ~IReflectedDependency() = default;
        virtual int Value() const = 0;
    };

    struct ReflectedDependency final : IReflectedDependency
    {
        int Value() const override
        {
            return 42;
        }
    };

    struct ReflectedConsumer
    {
        ReflectedConsumer(
            std::shared_ptr<IReflectedDependency> reflected,
            std::shared_ptr<SingletonService> singleton)
            : reflected(std::move(reflected)),
              singleton(std::move(singleton))
        {
        }

        std::shared_ptr<IReflectedDependency> reflected;
        std::shared_ptr<SingletonService> singleton;
    };

    struct AnnotatedConsumer
    {
        AnnotatedConsumer() = default;

        TGE_INJECT_CONSTRUCTOR
        explicit AnnotatedConsumer(std::shared_ptr<SingletonService> singleton)
            : singleton(std::move(singleton)),
              usedAnnotatedConstructor(true)
        {
        }

        std::shared_ptr<SingletonService> singleton;
        bool usedAnnotatedConstructor = false;
    };
#endif
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(DependentService, TGE::Inject<SingletonService>());
TGE_DECLARE_SERVICE_DEPENDENCIES(LocatorAwareService, TGE::InjectLocator());
TGE_DECLARE_SERVICE_DEPENDENCIES(CyclicA, TGE::Inject<CyclicB>());
TGE_DECLARE_SERVICE_DEPENDENCIES(CyclicB, TGE::Inject<CyclicA>());
#endif

TEST(ServiceLocatorTests, SingletonInstancesAreSharedAcrossScopes)
{
    SingletonService::nextId = 1;

    TGE::ServiceCollection collection;
    collection.AddSingleton<SingletonService>();
    auto provider = collection.BuildServiceProvider();

    auto rootFirst = provider->GetRequiredService<SingletonService>();
    auto rootSecond = provider->GetRequiredService<SingletonService>();
    EXPECT_EQ(rootFirst, rootSecond);

    auto scope = provider->CreateScope();
    auto scopedValue = scope->GetRequiredService<SingletonService>();
    EXPECT_EQ(rootFirst, scopedValue);
    EXPECT_EQ(rootFirst->id, 1);
}

TEST(ServiceLocatorTests, ScopedInstancesAreCachedPerScope)
{
    ScopedService::nextId = 1;

    TGE::ServiceCollection collection;
    collection.AddScoped<ScopedService>();
    auto provider = collection.BuildServiceProvider();

    auto rootFirst = provider->GetRequiredService<ScopedService>();
    auto rootSecond = provider->GetRequiredService<ScopedService>();
    ASSERT_EQ(rootFirst, rootSecond);

    auto scopeA = provider->CreateScope();
    auto scopeAFirst = scopeA->GetRequiredService<ScopedService>();
    auto scopeASecond = scopeA->GetRequiredService<ScopedService>();
    ASSERT_EQ(scopeAFirst, scopeASecond);

    auto scopeB = provider->CreateScope();
    auto scopeBValue = scopeB->GetRequiredService<ScopedService>();

    EXPECT_NE(rootFirst, scopeAFirst);
    EXPECT_NE(rootFirst, scopeBValue);
    EXPECT_NE(scopeAFirst, scopeBValue);
}

TEST(ServiceLocatorTests, TransientInstancesAreNeverCached)
{
    TransientService::nextId = 1;

    TGE::ServiceCollection collection;
    collection.AddTransient<TransientService>();
    auto provider = collection.BuildServiceProvider();

    auto first = provider->GetRequiredService<TransientService>();
    auto second = provider->GetRequiredService<TransientService>();
    EXPECT_NE(first, second);

    auto scope = provider->CreateScope();
    auto scopedFirst = scope->GetRequiredService<TransientService>();
    auto scopedSecond = scope->GetRequiredService<TransientService>();
    EXPECT_NE(scopedFirst, scopedSecond);
}

TEST(ServiceLocatorTests, ResolvesConstructorDependencies)
{
    SingletonService::nextId = 1;

    TGE::ServiceCollection collection;
    collection.AddSingleton<SingletonService>();
    collection.AddTransient<DependentService>();
    auto provider = collection.BuildServiceProvider();

    auto dependent = provider->GetRequiredService<DependentService>();
    ASSERT_NE(dependent, nullptr);
    ASSERT_NE(dependent->dependency, nullptr);

    auto singleton = provider->GetRequiredService<SingletonService>();
    EXPECT_EQ(dependent->dependency, singleton);
}

TEST(ServiceLocatorTests, ThrowsWhenConstructorDependencyIsMissing)
{
    TGE::ServiceCollection collection;
    collection.AddTransient<DependentService>();
    auto provider = collection.BuildServiceProvider();

    EXPECT_THROW(
        provider->GetRequiredService<DependentService>(),
        std::domain_error);
}

TEST(ServiceLocatorTests, InjectsLocatorReference)
{
    TGE::ServiceCollection collection;
    collection.AddTransient<LocatorAwareService>();
    auto provider = collection.BuildServiceProvider();

    auto rootInstance = provider->GetRequiredService<LocatorAwareService>();
    ASSERT_NE(rootInstance, nullptr);
    EXPECT_EQ(rootInstance->locatorAddress, provider.get());

    auto scope = provider->CreateScope();
    auto scopedInstance = scope->GetRequiredService<LocatorAwareService>();
    ASSERT_NE(scopedInstance, nullptr);
    EXPECT_EQ(scopedInstance->locatorAddress, scope.get());
}

TEST(ServiceLocatorTests, DetectsCyclicDependencies)
{
    TGE::ServiceCollection collection;
    collection.AddSingleton<CyclicA>();
    collection.AddSingleton<CyclicB>();
    auto provider = collection.BuildServiceProvider();

    EXPECT_THROW(provider->GetRequiredService<CyclicA>(), std::domain_error);
}

TEST(ServiceLocatorTests, PreventsDuplicateRegistrations)
{
    TGE::ServiceCollection collection;
    collection.AddSingleton<SingletonService>();

    EXPECT_THROW(collection.AddSingleton<SingletonService>(), std::domain_error);
}

TEST(ServiceLocatorTests, SupportsFactoryRegistrations)
{
    TGE::ServiceCollection collection;
    collection.AddSingleton<FactoryService>([](TGE::ServiceLocator&)
    {
        return std::make_shared<FactoryService>("payload");
    });

    auto provider = collection.BuildServiceProvider();
    auto first = provider->GetRequiredService<FactoryService>();
    auto second = provider->GetRequiredService<FactoryService>();

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first->payload, "payload");
}

TEST(ServiceLocatorTests, SupportsExistingInstanceRegistrations)
{
    auto instance = std::make_shared<FactoryService>("from-instance");

    TGE::ServiceCollection collection;
    collection.AddSingleton(instance);
    auto provider = collection.BuildServiceProvider();

    auto resolved = provider->GetRequiredService<FactoryService>();
    EXPECT_EQ(resolved, instance);
    EXPECT_EQ(resolved->payload, "from-instance");
}

TEST(ServiceLocatorTests, PreservesImplementationAddressForAdjustedBasePointers)
{
    TGE::ServiceCollection collection;
    collection.AddSingleton<IOffsetService, OffsetService>();
    auto provider = collection.BuildServiceProvider();

    auto service = provider->GetRequiredService<IOffsetService>();
    auto implementation = provider->GetRequiredService<OffsetService>();

    ASSERT_NE(service, nullptr);
    ASSERT_NE(implementation, nullptr);
    EXPECT_EQ(service.get(), static_cast<IOffsetService*>(implementation.get()));
    EXPECT_EQ(service->Value(), 17);
}

#if TGE_HAS_REFLECTION_DI
TEST(ServiceLocatorTests, ReflectionInjectsMultipleTypedDependenciesWithoutTraits)
{
    SingletonService::nextId = 1;

    TGE::ServiceCollection collection;
    collection.AddSingleton<IReflectedDependency, ReflectedDependency>();
    collection.AddSingleton<SingletonService>();
    collection.AddTransient<ReflectedConsumer>();
    auto provider = collection.BuildServiceProvider();

    auto consumer = provider->GetRequiredService<ReflectedConsumer>();

    ASSERT_NE(consumer, nullptr);
    ASSERT_NE(consumer->reflected, nullptr);
    ASSERT_NE(consumer->singleton, nullptr);
    EXPECT_EQ(consumer->reflected->Value(), 42);
    EXPECT_EQ(
        consumer->singleton,
        provider->GetRequiredService<SingletonService>());
}

TEST(ServiceLocatorTests, ReflectionAnnotationSelectsConstructorOverload)
{
    TGE::ServiceCollection collection;
    collection.AddSingleton<SingletonService>();
    collection.AddTransient<AnnotatedConsumer>();
    auto provider = collection.BuildServiceProvider();

    auto consumer = provider->GetRequiredService<AnnotatedConsumer>();

    ASSERT_NE(consumer, nullptr);
    EXPECT_TRUE(consumer->usedAnnotatedConstructor);
    EXPECT_EQ(
        consumer->singleton,
        provider->GetRequiredService<SingletonService>());
}
#endif
