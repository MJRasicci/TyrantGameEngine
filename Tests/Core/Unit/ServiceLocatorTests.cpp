#include <gtest/gtest.h>

#include "Services/ServiceCollection.hpp"
#include "Services/ServiceLocator.hpp"
#include "Services/ServiceProvider.hpp"
#include "Services/ServiceTraits.hpp"

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
}

TGE_DECLARE_SERVICE_DEPENDENCIES(DependentService, TGE::Inject<SingletonService>());
TGE_DECLARE_SERVICE_DEPENDENCIES(LocatorAwareService, TGE::InjectLocator());
TGE_DECLARE_SERVICE_DEPENDENCIES(CyclicA, TGE::Inject<CyclicB>());
TGE_DECLARE_SERVICE_DEPENDENCIES(CyclicB, TGE::Inject<CyclicA>());

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

TEST(ServiceLocatorTests, TraitsResolveServiceDependencies)
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

TEST(ServiceLocatorTests, TraitsInjectLocatorReference)
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
