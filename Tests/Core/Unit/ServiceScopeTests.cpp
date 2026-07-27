#include <gtest/gtest.h>

#include "TGE/Execution/Task.hpp"
#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceProvider.hpp"

#include <atomic>
#include <barrier>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#if !TGE_HAS_NATIVE_STD_EXECUTION
    #include <exec/static_thread_pool.hpp>
#endif

namespace
{
    void EndScope(const std::shared_ptr<TGE::ServiceScope>& scope)
    {
        auto result = TGE::Execution::SyncWait(scope->EndAsync());
        if (!result)
        {
            throw std::runtime_error(
                "Scope ending completed through cancellation.");
        }
    }

    class EventRecord
    {
    public:
        void Add(std::string event)
        {
            std::scoped_lock lock(mutex);
            events.emplace_back(std::move(event));
        }

        std::vector<std::string> Snapshot() const
        {
            std::scoped_lock lock(mutex);
            return events;
        }

    private:
        mutable std::mutex mutex;
        std::vector<std::string> events;
    };

    class BlockingCleanup
    {
    public:
        void Begin()
        {
            {
                std::scoped_lock lock(mutex);
                began = true;
            }
            condition.notify_all();
        }

        void WaitUntilBegun()
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [this] { return began; });
        }

        void WaitUntilReleased()
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [this] { return released; });
        }

        void Release()
        {
            {
                std::scoped_lock lock(mutex);
                released = true;
            }
            condition.notify_all();
        }

    private:
        std::mutex mutex;
        std::condition_variable condition;
        bool began = false;
        bool released = false;
    };

    struct TrackedScopedService
    {
        TrackedScopedService(
            std::shared_ptr<EventRecord> record,
            int identifier)
            : record(std::move(record)),
              identifier(identifier)
        {
        }

        ~TrackedScopedService()
        {
            record->Add("release:" + std::to_string(identifier));
        }

        std::shared_ptr<EventRecord> record;
        int identifier;
    };

    struct ConcurrentScopedService
    {
    };
}

TEST(ServiceScopeTests, EndsChildrenCleanupsAndServicesInReverseOrder)
{
    auto record = std::make_shared<EventRecord>();
    auto nextIdentifier = std::make_shared<int>(1);

    TGE::ServiceCollection collection;
    collection.AddScoped<TrackedScopedService>(
        [record, nextIdentifier](TGE::ServiceLocator&)
        {
            return std::make_shared<TrackedScopedService>(
                record,
                (*nextIdentifier)++);
        });
    auto provider = collection.BuildServiceProvider();

    auto parent = provider->CreateScope();
    auto parentService =
        parent->GetRequiredService<TrackedScopedService>();
    parent->RegisterCleanup([record]() -> TGE::Task<void>
    {
        record->Add("cleanup:parent:first");
        co_return;
    });
    parent->RegisterCleanup([record]() -> TGE::Task<void>
    {
        record->Add("cleanup:parent:second");
        co_return;
    });

    auto firstChild = parent->CreateScope();
    auto firstService =
        firstChild->GetRequiredService<TrackedScopedService>();
    firstChild->RegisterCleanup([record]() -> TGE::Task<void>
    {
        record->Add("cleanup:child:first");
        co_return;
    });

    auto secondChild = parent->CreateScope();
    auto secondService =
        secondChild->GetRequiredService<TrackedScopedService>();
    secondChild->RegisterCleanup([record]() -> TGE::Task<void>
    {
        record->Add("cleanup:child:second");
        co_return;
    });

    parentService.reset();
    firstService.reset();
    secondService.reset();

    EndScope(parent);

    EXPECT_EQ(parent->GetState(), TGE::ServiceScopeState::Ended);
    EXPECT_EQ(firstChild->GetState(), TGE::ServiceScopeState::Ended);
    EXPECT_EQ(secondChild->GetState(), TGE::ServiceScopeState::Ended);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "cleanup:child:second",
            "release:3",
            "cleanup:child:first",
            "release:2",
            "cleanup:parent:second",
            "cleanup:parent:first",
            "release:1"
        }));
}

TEST(ServiceScopeTests, RejectsScopeWorkAsSoonAsEndingBegins)
{
    auto blocker = std::make_shared<BlockingCleanup>();

    TGE::ServiceCollection collection;
    collection.AddScoped<ConcurrentScopedService>();
    auto provider = collection.BuildServiceProvider();
    auto scope = provider->CreateScope();
    auto child = scope->CreateScope();

    scope->RegisterCleanup([blocker]() -> TGE::Task<void>
    {
        blocker->Begin();
        blocker->WaitUntilReleased();
        co_return;
    });

    std::exception_ptr endingFailure;
    std::jthread endingThread([&]
    {
        try
        {
            EndScope(scope);
        }
        catch (...)
        {
            endingFailure = std::current_exception();
        }
    });

    blocker->WaitUntilBegun();

    EXPECT_EQ(scope->GetState(), TGE::ServiceScopeState::Ending);
    EXPECT_THROW(
        scope->GetRequiredService<ConcurrentScopedService>(),
        std::logic_error);
    EXPECT_THROW(
        child->GetRequiredService<ConcurrentScopedService>(),
        std::logic_error);
    EXPECT_THROW(scope->CreateScope(), std::logic_error);
    EXPECT_THROW(
        scope->RegisterCleanup([]() -> TGE::Task<void> { co_return; }),
        std::logic_error);

    blocker->Release();
    endingThread.join();

    EXPECT_EQ(endingFailure, nullptr);
    EXPECT_EQ(scope->GetState(), TGE::ServiceScopeState::Ended);
    EXPECT_EQ(child->GetState(), TGE::ServiceScopeState::Ended);
}

TEST(ServiceScopeTests, SharesOneCompletionAcrossConcurrentEndRequests)
{
    auto blocker = std::make_shared<BlockingCleanup>();
    std::atomic<int> cleanupCount = 0;

    TGE::ServiceCollection collection;
    auto provider = collection.BuildServiceProvider();
    auto scope = provider->CreateScope();

    scope->RegisterCleanup(
        [blocker, &cleanupCount]() -> TGE::Task<void>
        {
            ++cleanupCount;
            blocker->Begin();
            blocker->WaitUntilReleased();
            co_return;
        });

    constexpr std::size_t callerCount = 8;
    std::barrier startBarrier(
        static_cast<std::ptrdiff_t>(callerCount + 1));
    std::vector<std::exception_ptr> failures(callerCount);
    std::vector<std::jthread> callers;
    callers.reserve(callerCount);

    for (std::size_t index = 0; index < callerCount; ++index)
    {
        callers.emplace_back([&, index]
        {
            startBarrier.arrive_and_wait();
            try
            {
                EndScope(scope);
            }
            catch (...)
            {
                failures[index] = std::current_exception();
            }
        });
    }

    startBarrier.arrive_and_wait();
    blocker->WaitUntilBegun();
    blocker->Release();

    for (auto& caller : callers)
    {
        caller.join();
    }

    EXPECT_EQ(cleanupCount.load(), 1);
    EXPECT_EQ(scope->GetState(), TGE::ServiceScopeState::Ended);
    for (const auto& failure : failures)
    {
        EXPECT_EQ(failure, nullptr);
    }

    EXPECT_NO_THROW(EndScope(scope));
    EXPECT_EQ(cleanupCount.load(), 1);
}

#if !TGE_HAS_NATIVE_STD_EXECUTION
TEST(ServiceScopeTests, ConcurrentWaitersDoNotBlockASuspendedCleanupDispatcher)
{
    std::atomic<int> cleanupCount = 0;
    exec::static_thread_pool dispatcher { 1 };
    auto scheduler = dispatcher.get_scheduler();

    TGE::ServiceCollection collection;
    auto provider = collection.BuildServiceProvider();
    auto scope = provider->CreateScope();

    scope->RegisterCleanup(
        [scheduler, &cleanupCount]() -> TGE::Task<void>
        {
            co_await stdexec::schedule(scheduler);
            ++cleanupCount;
        });

    auto bothEnds =
        stdexec::schedule(scheduler) |
        stdexec::let_value(
            [scope]
            {
                return stdexec::when_all(
                    scope->EndAsync(),
                    scope->EndAsync());
            });
    auto result = TGE::Execution::SyncWait(std::move(bothEnds));

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(cleanupCount.load(), 1);
    EXPECT_EQ(scope->GetState(), TGE::ServiceScopeState::Ended);
}
#endif

TEST(ServiceScopeTests, ContinuesCleanupAndSharesTheFirstFailure)
{
    auto record = std::make_shared<EventRecord>();

    TGE::ServiceCollection collection;
    auto provider = collection.BuildServiceProvider();
    auto scope = provider->CreateScope();

    scope->RegisterCleanup([record]() -> TGE::Task<void>
    {
        record->Add("cleanup:continued");
        co_return;
    });
    scope->RegisterCleanup([record]() -> TGE::Task<void>
    {
        record->Add("cleanup:failed");
        throw std::runtime_error("scope cleanup failed");
        co_return;
    });

    EXPECT_THROW(EndScope(scope), std::runtime_error);
    EXPECT_EQ(scope->GetState(), TGE::ServiceScopeState::Ended);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "cleanup:failed",
            "cleanup:continued"
        }));

    try
    {
        EndScope(scope);
        FAIL() << "Expected the shared scope-ending failure.";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_STREQ(error.what(), "scope cleanup failed");
    }
}

TEST(ServiceScopeTests, DestructorSynchronouslyEndsAnActiveScope)
{
    std::atomic<int> cleanupCount = 0;

    TGE::ServiceCollection collection;
    auto provider = collection.BuildServiceProvider();

    {
        auto scope = provider->CreateScope();
        scope->RegisterCleanup([&cleanupCount]() -> TGE::Task<void>
        {
            ++cleanupCount;
            co_return;
        });
    }

    EXPECT_EQ(cleanupCount.load(), 1);
}

TEST(ServiceScopeTests, ConcurrentResolutionActivatesOneScopedInstance)
{
    std::atomic<int> activations = 0;

    TGE::ServiceCollection collection;
    collection.AddScoped<ConcurrentScopedService>(
        [&activations](TGE::ServiceLocator&)
        {
            ++activations;
            return std::make_shared<ConcurrentScopedService>();
        });
    auto provider = collection.BuildServiceProvider();
    auto scope = provider->CreateScope();

    constexpr std::size_t callerCount = 12;
    std::barrier startBarrier(
        static_cast<std::ptrdiff_t>(callerCount + 1));
    std::vector<std::shared_ptr<ConcurrentScopedService>> instances(
        callerCount);
    std::vector<std::jthread> callers;
    callers.reserve(callerCount);

    for (std::size_t index = 0; index < callerCount; ++index)
    {
        callers.emplace_back([&, index]
        {
            startBarrier.arrive_and_wait();
            instances[index] =
                scope->GetRequiredService<ConcurrentScopedService>();
        });
    }

    startBarrier.arrive_and_wait();
    for (auto& caller : callers)
    {
        caller.join();
    }

    ASSERT_EQ(activations.load(), 1);
    ASSERT_NE(instances.front(), nullptr);
    for (const auto& instance : instances)
    {
        EXPECT_EQ(instance, instances.front());
    }
}
