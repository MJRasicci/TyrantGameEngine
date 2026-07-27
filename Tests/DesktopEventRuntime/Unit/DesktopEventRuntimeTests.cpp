#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "FakeDesktopEventPump.hpp"
#include "Internal/Desktop/DesktopEventRuntime.hpp"
#include "TGE/Execution/Task.hpp"

namespace
{
    using namespace std::chrono_literals;

    template<class TSender>
    auto WaitValue(TSender&& sender)
    {
        auto completion = TGE::Execution::SyncWait(
            std::forward<TSender>(sender));
        if (!completion)
        {
            throw std::runtime_error(
                "Desktop event operation completed through cancellation.");
        }
        return std::get<0>(std::move(*completion));
    }

    TEST(
        DesktopEventRuntimeTests,
        RunDrivesTheEntirePumpLifecycleOnItsCallingThread)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* fake = pump.get();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        const auto caller = std::this_thread::get_id();
        std::atomic<bool> reachedPump { false };

        std::jthread stopper(
            [&]
            {
                reachedPump.store(
                    fake->WaitUntilPumping(),
                    std::memory_order_release);
                runtime.RequestStop();
            });

        const auto result = runtime.Run();
        stopper.join();

        ASSERT_TRUE(result) << result.error().message;
        EXPECT_TRUE(reachedPump.load(std::memory_order_acquire));
        EXPECT_EQ(fake->StartCount(), 1U);
        EXPECT_GE(fake->PumpCount(), 1U);
        EXPECT_EQ(fake->StopCount(), 1U);
        EXPECT_EQ(fake->StartThread(), caller);
        EXPECT_EQ(fake->LastPumpThread(), caller);
        EXPECT_EQ(fake->StopThread(), caller);
        EXPECT_FALSE(runtime.IsEventThread());
        EXPECT_FALSE(runtime.IsAccepting());
    }

    TEST(
        DesktopEventRuntimeTests,
        ConcurrentSubmittersAreSerializedOnTheRunner)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* fake = pump.get();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        TGE::Internal::DesktopEventResult runResult;
        std::thread::id runnerThread;

        std::jthread runner(
            [&](std::stop_token stopping)
            {
                runnerThread = std::this_thread::get_id();
                runResult = runtime.Run(stopping);
            });
        ASSERT_TRUE(fake->WaitUntilStarted());

        constexpr std::size_t callerCount = 16;
        std::barrier start(
            static_cast<std::ptrdiff_t>(callerCount));
        std::atomic<std::size_t> active { 0 };
        std::atomic<std::size_t> maximumActive { 0 };
        std::mutex observedMutex;
        std::vector<std::thread::id> operationThreads;
        std::vector<std::future<std::size_t>> callers;
        callers.reserve(callerCount);

        for (std::size_t index = 0; index < callerCount; ++index)
        {
            callers.emplace_back(std::async(
                std::launch::async,
                [&, index]
                {
                    start.arrive_and_wait();
                    return WaitValue(runtime.Submit(
                        [&, index]
                        {
                            const auto concurrent =
                                active.fetch_add(1) + 1;
                            auto maximum = maximumActive.load();
                            while (
                                concurrent > maximum &&
                                !maximumActive.compare_exchange_weak(
                                    maximum,
                                    concurrent))
                            {
                            }

                            {
                                std::scoped_lock lock(observedMutex);
                                operationThreads.emplace_back(
                                    std::this_thread::get_id());
                            }
                            std::this_thread::sleep_for(1ms);
                            active.fetch_sub(1);
                            return index;
                        }));
                }));
        }

        std::vector<std::size_t> results;
        results.reserve(callerCount);
        for (auto& caller : callers)
        {
            results.emplace_back(caller.get());
        }

        runtime.RequestStop();
        runner.join();

        ASSERT_TRUE(runResult) << runResult.error().message;
        EXPECT_EQ(maximumActive.load(), 1U);
        ASSERT_EQ(operationThreads.size(), callerCount);
        EXPECT_TRUE(std::ranges::all_of(
            operationThreads,
            [runnerThread](std::thread::id thread)
            {
                return thread == runnerThread;
            }));
        std::ranges::sort(results);
        for (std::size_t index = 0; index < callerCount; ++index)
        {
            EXPECT_EQ(results[index], index);
        }
    }

    TEST(
        DesktopEventRuntimeTests,
        SubmitReentryExecutesInlineOnTheEventThread)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        std::vector<std::string> order;

        ASSERT_TRUE(runtime.Post(
            [&]
            {
                order.emplace_back("outer-before");
                EXPECT_TRUE(runtime.IsEventThread());

                const auto value = WaitValue(runtime.Submit(
                    [&]
                    {
                        EXPECT_TRUE(runtime.IsEventThread());
                        order.emplace_back("inner");
                        return 42;
                    }));

                EXPECT_EQ(value, 42);
                order.emplace_back("outer-after");
                runtime.RequestStop();
            }));

        const auto result = runtime.Run();

        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(
            order,
            (std::vector<std::string> {
                "outer-before",
                "inner",
                "outer-after"
            }));
    }

    TEST(
        DesktopEventRuntimeTests,
        PostFromTheEventThreadAlwaysRunsOnALaterTurn)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        std::vector<std::string> order;
        bool outerActive = false;
        bool postObservedOuterActive = true;

        ASSERT_TRUE(runtime.Post(
            [&]
            {
                outerActive = true;
                order.emplace_back("outer-before");

                EXPECT_TRUE(runtime.Post(
                    [&]
                    {
                        postObservedOuterActive = outerActive;
                        order.emplace_back("posted");
                        runtime.RequestStop();
                    }));

                order.emplace_back("outer-after");
                outerActive = false;
            }));

        const auto result = runtime.Run();

        ASSERT_TRUE(result) << result.error().message;
        EXPECT_FALSE(postObservedOuterActive);
        EXPECT_EQ(
            order,
            (std::vector<std::string> {
                "outer-before",
                "outer-after",
                "posted"
            }));
    }

    TEST(
        DesktopEventRuntimeTests,
        SemanticWorkFromOneNativeEventPrecedesTheNextNativeEvent)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* fake = pump.get();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        std::vector<std::string> order;

        fake->QueueEvent(
            [&]
            {
                order.emplace_back("native-input");
                EXPECT_TRUE(runtime.Post(
                    [&]
                    {
                        order.emplace_back("published-input");
                    }));
            });
        fake->QueueEvent(
            [&]
            {
                order.emplace_back("native-close");
                runtime.RequestStop();
            });

        const auto result = runtime.Run();

        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(
            order,
            (std::vector<std::string> {
                "native-input",
                "published-input",
                "native-close"
            }));
    }

    TEST(
        DesktopEventRuntimeTests,
        SubmittingFromAnotherThreadWakesTheBlockingPump)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* fake = pump.get();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        TGE::Internal::DesktopEventResult runResult;
        std::thread::id operationThread;

        std::jthread runner(
            [&](std::stop_token stopping)
            {
                runResult = runtime.Run(stopping);
            });
        ASSERT_TRUE(fake->WaitUntilPumping());

        const auto submittingThread = std::this_thread::get_id();
        const auto value = WaitValue(runtime.Submit(
            [&]
            {
                operationThread = std::this_thread::get_id();
                return 7;
            }));
        runtime.RequestStop();
        runner.join();

        ASSERT_TRUE(runResult) << runResult.error().message;
        EXPECT_EQ(value, 7);
        EXPECT_NE(operationThread, submittingThread);
        EXPECT_GE(fake->WakeCount(), 1U);
        EXPECT_EQ(fake->FirstWakeThread(), submittingThread);
    }

    TEST(
        DesktopEventRuntimeTests,
        ASecondRunnerIsRejectedWithoutTouchingThePump)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* fake = pump.get();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        TGE::Internal::DesktopEventResult firstResult;

        std::jthread runner(
            [&](std::stop_token stopping)
            {
                firstResult = runtime.Run(stopping);
            });
        ASSERT_TRUE(fake->WaitUntilPumping());

        const auto secondResult = runtime.Run();
        ASSERT_FALSE(secondResult);
        EXPECT_EQ(
            secondResult.error().code,
            TGE::Internal::DesktopEventErrorCode::InvalidState);
        EXPECT_EQ(
            secondResult.error().message,
            "A desktop event runtime can only be driven once.");
        EXPECT_EQ(fake->StartCount(), 1U);

        runtime.RequestStop();
        runner.join();
        ASSERT_TRUE(firstResult) << firstResult.error().message;
        EXPECT_EQ(fake->StartCount(), 1U);
        EXPECT_EQ(fake->StopCount(), 1U);
    }

    TEST(
        DesktopEventRuntimeTests,
        StopIsIdempotentAndDrainsCommandsAcceptedBeforeAdmissionClosed)
    {
        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* fake = pump.get();
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));
        std::vector<int> order;

        ASSERT_TRUE(runtime.Post(
            [&]
            {
                order.emplace_back(1);
                runtime.RequestStop();
                runtime.RequestStop();
            }));
        ASSERT_TRUE(runtime.Post(
            [&]
            {
                order.emplace_back(2);
            }));

        const auto result = runtime.Run();

        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(order, (std::vector<int> { 1, 2 }));
        EXPECT_EQ(fake->StopCount(), 1U);
        EXPECT_EQ(fake->WakeCount(), 1U);
        EXPECT_FALSE(runtime.Post([] {}));

        try
        {
            static_cast<void>(WaitValue(runtime.Submit(
                []
                {
                    return 3;
                })));
            FAIL() << "A command submitted after stop unexpectedly ran.";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_STREQ(
                error.what(),
                "The desktop event runtime is not accepting work.");
        }
    }

    TEST(
        DesktopEventRuntimeTests,
        StartupAndRepeatedRunErrorsAreStable)
    {
        EXPECT_THROW(
            TGE::Internal::DesktopEventRuntime(nullptr),
            std::invalid_argument);

        auto pump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* fake = pump.get();
        fake->SetStartResult(std::unexpected(
            TGE::Internal::DesktopEventError {
                .code =
                    TGE::Internal::DesktopEventErrorCode::PumpStartFailed,
                .message = "synthetic startup failure"
            }));
        TGE::Internal::DesktopEventRuntime runtime(std::move(pump));

        const auto firstResult = runtime.Run();
        ASSERT_FALSE(firstResult);
        EXPECT_EQ(
            firstResult.error(),
            (TGE::Internal::DesktopEventError {
                .code =
                    TGE::Internal::DesktopEventErrorCode::PumpStartFailed,
                .message = "synthetic startup failure"
            }));
        EXPECT_EQ(fake->StartCount(), 1U);
        EXPECT_EQ(fake->PumpCount(), 0U);
        EXPECT_EQ(fake->StopCount(), 0U);

        const auto secondResult = runtime.Run();
        ASSERT_FALSE(secondResult);
        EXPECT_EQ(
            secondResult.error(),
            (TGE::Internal::DesktopEventError {
                .code =
                    TGE::Internal::DesktopEventErrorCode::InvalidState,
                .message =
                    "A desktop event runtime can only be driven once."
            }));
    }
}
