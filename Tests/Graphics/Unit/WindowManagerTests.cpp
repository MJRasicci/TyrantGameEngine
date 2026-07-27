#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "FakeWindowPlatform.hpp"
#include "Internal/Desktop/DesktopEventRuntime.hpp"
#include "Internal/Graphics/WindowManager.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Graphics.hpp"

namespace
{
    template<class TValue>
    TValue Wait(TGE::Task<TValue> task)
    {
        auto completion =
            TGE::Execution::SyncWait(std::move(task));
        if (!completion)
        {
            throw std::runtime_error(
                "Window task completed through cancellation.");
        }
        return std::get<0>(std::move(*completion));
    }

    std::shared_ptr<TGE::IWindow> Create(
        TGE::IWindowManager& manager,
        TGE::WindowDescriptor descriptor = {})
    {
        auto result =
            Wait(manager.CreateWindowAsync(std::move(descriptor)));
        if (!result)
        {
            throw std::runtime_error(result.error().message);
        }
        return std::move(*result);
    }

    class BlockingWindowPlatformSink final
        : public TGE::Internal::IWindowPlatformEventSink
    {
    public:
        void OnPlatformConfigurationChanged(
            TGE::WindowId,
            TGE::WindowConfiguration) noexcept override
        {
            std::unique_lock lock(mutex);
            callbackEntered = true;
            condition.notify_all();
            condition.wait(
                lock,
                [this]
                {
                    return callbackReleased;
                });
        }

        bool OnPlatformCloseRequested(
            TGE::WindowId,
            TGE::WindowCloseReason) noexcept override
        {
            return true;
        }

        void OnPlatformClosed(
            TGE::WindowId,
            TGE::WindowCloseReason) noexcept override
        {
        }

        bool WaitUntilCallbackEntered()
        {
            std::unique_lock lock(mutex);
            return condition.wait_for(
                lock,
                std::chrono::seconds(2),
                [this]
                {
                    return callbackEntered;
                });
        }

        void ReleaseCallback()
        {
            {
                std::scoped_lock lock(mutex);
                callbackReleased = true;
            }
            condition.notify_all();
        }

    private:
        std::mutex mutex;
        std::condition_variable condition;
        bool callbackEntered { false };
        bool callbackReleased { false };
    };

    TEST(
        WindowPlatformContract,
        DetachingEventSinkWaitsForEnteredCallbacksToReturn)
    {
        TGE::Tests::FakeDesktopEventPump pump;
        TGE::Tests::FakeWindowPlatform platform(pump);
        BlockingWindowPlatformSink sink;
        platform.SetEventSink(&sink);

        const auto id = TGE::WindowId::FromValue(1);
        ASSERT_TRUE(platform.CreateWindow(id, TGE::WindowDescriptor {}));

        auto mutation = std::async(
            std::launch::async,
            [&platform, id]
            {
                return platform.SetTitle(id, "Blocked callback");
            });

        const auto callbackEntered = sink.WaitUntilCallbackEntered();
        if (!callbackEntered)
        {
            sink.ReleaseCallback();
        }
        ASSERT_TRUE(callbackEntered);

        std::promise<void> detachStarted;
        auto detachStartedFuture = detachStarted.get_future();
        auto detach = std::async(
            std::launch::async,
            [
                &platform,
                detachStarted = std::move(detachStarted)
            ]() mutable
            {
                detachStarted.set_value();
                platform.SetEventSink(nullptr);
            });
        detachStartedFuture.wait();

        EXPECT_EQ(
            detach.wait_for(std::chrono::milliseconds(25)),
            std::future_status::timeout);

        sink.ReleaseCallback();

        ASSERT_EQ(
            mutation.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
        ASSERT_TRUE(mutation.get());
        ASSERT_EQ(
            detach.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
        detach.get();
    }

    struct ManagerFixture : testing::Test
    {
        void SetUp() override
        {
            auto eventPump =
                std::make_unique<TGE::Tests::FakeDesktopEventPump>();
            pump = eventPump.get();
            runtime =
                std::make_shared<TGE::Internal::DesktopEventRuntime>(
                    std::move(eventPump));

            auto platform =
                std::make_unique<TGE::Tests::FakeWindowPlatform>(*pump);
            fake = platform.get();
            manager =
                std::make_unique<TGE::Internal::WindowManager>(
                    runtime,
                    std::move(platform));

            runner = std::jthread(
                [runtime = runtime](std::stop_token stopping)
                {
                    (void)runtime->Run(stopping);
                });
            ASSERT_TRUE(pump->WaitUntilStarted());
        }

        void TearDown() override
        {
            manager.reset();
            runtime->RequestStop();
            runner.join();
            runtime.reset();
        }

        TGE::Tests::FakeDesktopEventPump* pump { nullptr };
        TGE::Tests::FakeWindowPlatform* fake { nullptr };
        std::shared_ptr<TGE::Internal::DesktopEventRuntime> runtime;
        std::unique_ptr<TGE::Internal::WindowManager> manager;
        std::jthread runner;
    };

    TEST_F(
        ManagerFixture,
        CreationPublishesLogicalAndFramebufferGeometryAndCapabilities)
    {
        fake->initialScale = { 1.5F, 2.0F };

        TGE::WindowDescriptor descriptor {
            .title = "Inspector",
            .bounds = {
                .position = { 24.0F, 48.0F },
                .size = { 640.0F, 360.0F }
            }
        };

        const auto window = Create(*manager, descriptor);

        EXPECT_EQ(window->RequestedDescriptor(), descriptor);
        EXPECT_EQ(
            window->Geometry().logicalBounds,
            descriptor.bounds);
        EXPECT_EQ(
            window->Geometry().framebufferSize,
            (TGE::FramebufferSize { 960, 720 }));
        EXPECT_EQ(
            window->Geometry().scale,
            (TGE::WindowScale { 1.5F, 2.0F }));
        EXPECT_TRUE(window->Capabilities().inputControl);
        EXPECT_EQ(
            window->LifecycleState(),
            TGE::WindowLifecycleState::Open);
        ASSERT_EQ(manager->Windows().size(), 1U);
        EXPECT_EQ(
            manager->FindWindow(window->Id()),
            window);
    }

    TEST_F(
        ManagerFixture,
        CreationNormalizesUnsupportedPreferencesAndMutationReportsUnsupported)
    {
        fake->capabilities.positioning = false;
        fake->capabilities.fullscreen = false;
        fake->capabilities.decorations = false;

        TGE::WindowDescriptor descriptor {
            .title = "Portable",
            .bounds = {
                .position = { 100.0F, 200.0F },
                .size = { 800.0F, 600.0F }
            },
            .state = TGE::WindowState::Fullscreen
        };
        const auto window = Create(*manager, descriptor);
        const auto effective = window->EffectiveConfiguration();

        EXPECT_EQ(
            effective.geometry.logicalBounds.position,
            TGE::LogicalPoint {});
        EXPECT_EQ(effective.state, TGE::WindowState::Normal);
        EXPECT_FALSE(effective.chrome.decorations);
        EXPECT_FALSE(window->Capabilities().positioning);

        const auto mutation =
            Wait(window->SetStateAsync(TGE::WindowState::Fullscreen));
        ASSERT_FALSE(mutation);
        EXPECT_EQ(
            mutation.error().code,
            TGE::WindowErrorCode::Unsupported);
        EXPECT_EQ(
            window->LifecycleState(),
            TGE::WindowLifecycleState::Open);
    }

    TEST_F(
        ManagerFixture,
        ConcurrentCallersAreSerializedOntoOnePlatformThread)
    {
        const auto window = Create(*manager);
        fake->SetCallDelay(std::chrono::milliseconds(2));

        constexpr std::size_t callerCount = 12;
        std::barrier start(static_cast<std::ptrdiff_t>(callerCount));
        std::atomic<std::size_t> successful { 0 };
        std::vector<std::thread> callers;
        callers.reserve(callerCount);

        for (std::size_t index = 0; index < callerCount; ++index)
        {
            callers.emplace_back(
                [&, index]
                {
                    start.arrive_and_wait();
                    auto result = Wait(window->SetTitleAsync(
                        "Title " + std::to_string(index)));
                    if (result)
                    {
                        successful.fetch_add(1);
                    }
                });
        }
        for (auto& caller : callers)
        {
            caller.join();
        }

        EXPECT_EQ(successful.load(), callerCount);
        EXPECT_EQ(fake->MaximumConcurrentCalls(), 1U);

        const auto callThreads = fake->PlatformCallThreads();
        ASSERT_GE(callThreads.size(), callerCount + 1);
        const std::set<std::thread::id> uniqueThreads(
            callThreads.begin(),
            callThreads.end());
        EXPECT_EQ(uniqueThreads.size(), 1U);
        EXPECT_EQ(*uniqueThreads.begin(), pump->RunnerThread());
    }

    TEST_F(
        ManagerFixture,
        ScaleResizeAndAggregateEventsHaveCoherentDeterministicOrder)
    {
        const auto window = Create(*manager);
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::string> order;
        TGE::WindowGeometry scaleGeometry;
        TGE::WindowGeometry resizeGeometry;

        auto scaleSubscription = window->SubscribeScaleChanged(
            [&](const TGE::WindowScaleChangedEvent& event)
            {
                std::scoped_lock lock(mutex);
                order.emplace_back("scale");
                scaleGeometry = event.current;
            });
        auto resizeSubscription = window->SubscribeResized(
            [&](const TGE::WindowResizedEvent& event)
            {
                std::scoped_lock lock(mutex);
                order.emplace_back("resize");
                resizeGeometry = event.current;
            });
        auto configurationSubscription =
            window->SubscribeConfigurationChanged(
                [&](const TGE::WindowConfigurationChangedEvent&)
                {
                    {
                        std::scoped_lock lock(mutex);
                        order.emplace_back("configuration");
                    }
                    changed.notify_all();
                });

        auto configuration = window->EffectiveConfiguration();
        configuration.geometry.logicalBounds.size = { 900.0F, 500.0F };
        configuration.geometry.scale = { 2.0F, 2.0F };
        configuration.geometry.framebufferSize = { 1800, 1000 };
        fake->QueueConfiguration(window->Id(), configuration);

        {
            std::unique_lock lock(mutex);
            ASSERT_TRUE(changed.wait_for(
                lock,
                std::chrono::seconds(2),
                [&]
                {
                    return order.size() == 3;
                }));
            EXPECT_EQ(
                order,
                (std::vector<std::string> {
                    "scale",
                    "resize",
                    "configuration"
                }));
        }
        EXPECT_EQ(scaleGeometry, configuration.geometry);
        EXPECT_EQ(resizeGeometry, configuration.geometry);
    }

    TEST_F(
        ManagerFixture,
        ConfigurationBurstsCoalesceToTheirLatestCoherentSnapshot)
    {
        const auto window = Create(*manager);
        std::mutex mutex;
        std::condition_variable changed;
        std::size_t resizeEvents = 0;
        TGE::WindowGeometry observed;

        auto resizeSubscription = window->SubscribeResized(
            [&](const TGE::WindowResizedEvent& event)
            {
                std::scoped_lock lock(mutex);
                ++resizeEvents;
                observed = event.current;
                changed.notify_all();
            });

        auto first = window->EffectiveConfiguration();
        first.geometry.logicalBounds.size = { 700.0F, 400.0F };
        first.geometry.framebufferSize = { 700, 400 };
        auto latest = first;
        latest.geometry.logicalBounds.size = { 900.0F, 600.0F };
        latest.geometry.framebufferSize = { 900, 600 };

        fake->QueueConfigurations(
            window->Id(),
            { first, latest });

        {
            std::unique_lock lock(mutex);
            ASSERT_TRUE(changed.wait_for(
                lock,
                std::chrono::seconds(2),
                [&]
                {
                    return resizeEvents > 0;
                }));
        }

        // Give an incorrectly queued second delivery an opportunity to run.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        EXPECT_EQ(resizeEvents, 1U);
        EXPECT_EQ(observed, latest.geometry);
        EXPECT_EQ(window->Geometry(), latest.geometry);
    }

    TEST_F(
        ManagerFixture,
        CloseRequestsAreCancellableAndSuccessfulCloseIsTerminal)
    {
        const auto window = Create(*manager);
        std::atomic<std::size_t> closed { 0 };

        auto cancellation = window->SubscribeCloseRequested(
            [](TGE::WindowCloseRequestedEvent& event)
            {
                event.Cancel();
            });
        auto closedSubscription = window->SubscribeClosed(
            [&](const TGE::WindowClosedEvent&)
            {
                closed.fetch_add(1);
            });

        const auto cancelled = Wait(window->RequestCloseAsync());
        ASSERT_TRUE(cancelled);
        EXPECT_EQ(*cancelled, TGE::WindowOperationStatus::Cancelled);
        EXPECT_EQ(
            window->LifecycleState(),
            TGE::WindowLifecycleState::Open);
        EXPECT_EQ(manager->FindWindow(window->Id()), window);

        cancellation.Reset();
        const auto applied = Wait(window->RequestCloseAsync());
        ASSERT_TRUE(applied);
        EXPECT_EQ(*applied, TGE::WindowOperationStatus::Applied);
        EXPECT_EQ(closed.load(), 1U);
        EXPECT_EQ(
            window->LifecycleState(),
            TGE::WindowLifecycleState::Destroyed);
        EXPECT_FALSE(manager->FindWindow(window->Id()));
        EXPECT_TRUE(manager->Windows().empty());

        const auto afterClose = Wait(window->ShowAsync());
        ASSERT_FALSE(afterClose);
        EXPECT_EQ(
            afterClose.error().code,
            TGE::WindowErrorCode::WindowDestroyed);
    }

    TEST_F(
        ManagerFixture,
        SubscribersRunInRegistrationOrderAndShareCloseCancellationState)
    {
        const auto window = Create(*manager);
        std::vector<int> order;
        bool secondObservedCancellation = false;

        auto first = window->SubscribeCloseRequested(
            [&](TGE::WindowCloseRequestedEvent& event)
            {
                order.emplace_back(1);
                event.Cancel();
            });
        auto second = window->SubscribeCloseRequested(
            [&](TGE::WindowCloseRequestedEvent& event)
            {
                order.emplace_back(2);
                secondObservedCancellation = event.IsCancelled();
            });

        const auto result = Wait(window->RequestCloseAsync());
        ASSERT_TRUE(result);
        EXPECT_EQ(*result, TGE::WindowOperationStatus::Cancelled);
        EXPECT_EQ(order, (std::vector<int> { 1, 2 }));
        EXPECT_TRUE(secondObservedCancellation);
    }

    TEST_F(
        ManagerFixture,
        EventCallbacksMaySynchronouslyReenterTheWindowFacade)
    {
        const auto window = Create(*manager);
        auto subscription = window->SubscribeCloseRequested(
            [&](TGE::WindowCloseRequestedEvent& event)
            {
                const auto result =
                    Wait(window->SetTitleAsync("Updated in callback"));
                EXPECT_TRUE(result);
                event.Cancel();
            });

        const auto close = Wait(window->RequestCloseAsync());
        ASSERT_TRUE(close);
        EXPECT_EQ(*close, TGE::WindowOperationStatus::Cancelled);
        EXPECT_EQ(window->Title(), "Updated in callback");
    }

    TEST_F(
        ManagerFixture,
        OverlappingModalWindowsRestoreInputOnlyAfterLastSuppressorCloses)
    {
        const auto parent = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Parent" });
        std::vector<bool> inputStates;
        auto inputSubscription = parent->SubscribeInputChanged(
            [&](const TGE::WindowInputChangedEvent& event)
            {
                inputStates.emplace_back(event.enabled);
            });

        TGE::WindowDescriptor modalDescriptor {
            .title = "Modal",
            .role = TGE::WindowRole::Modal,
            .parent = parent->Id(),
            .modality = TGE::WindowModality::DisableParent
        };
        const auto firstModal = Create(*manager, modalDescriptor);
        modalDescriptor.title = "Second modal";
        const auto secondModal = Create(*manager, modalDescriptor);

        EXPECT_FALSE(parent->IsInputEnabled());

        ASSERT_TRUE(Wait(
            manager->DestroyWindowAsync(firstModal->Id())));
        EXPECT_FALSE(parent->IsInputEnabled());

        ASSERT_TRUE(Wait(
            manager->DestroyWindowAsync(secondModal->Id())));
        EXPECT_TRUE(parent->IsInputEnabled());
        EXPECT_EQ(
            inputStates,
            (std::vector<bool> { false, true }));
    }

    TEST_F(
        ManagerFixture,
        ParentTreeModalitySuppressesAncestorsButNotUnrelatedWindows)
    {
        const auto root = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Root" });
        const auto child = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Child",
                .role = TGE::WindowRole::Child,
                .parent = root->Id()
            });
        const auto unrelated = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Unrelated" });
        const auto modal = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Nested modal",
                .role = TGE::WindowRole::Modal,
                .parent = child->Id(),
                .modality = TGE::WindowModality::DisableParentTree
            });

        EXPECT_FALSE(root->IsInputEnabled());
        EXPECT_FALSE(child->IsInputEnabled());
        EXPECT_TRUE(unrelated->IsInputEnabled());
        EXPECT_TRUE(modal->IsInputEnabled());

        ASSERT_TRUE(Wait(manager->DestroyWindowAsync(modal->Id())));
        EXPECT_TRUE(root->IsInputEnabled());
        EXPECT_TRUE(child->IsInputEnabled());
    }

    TEST_F(
        ManagerFixture,
        DetachingAnAncestorReconcilesNestedParentTreeSuppression)
    {
        const auto root = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Root" });
        const auto parent = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Parent",
                .role = TGE::WindowRole::Child,
                .parent = root->Id()
            });
        const auto retainedBranch = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Retained branch",
                .role = TGE::WindowRole::Child,
                .parent = parent->Id()
            });
        const auto modal = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Nested modal",
                .role = TGE::WindowRole::Modal,
                .parent = retainedBranch->Id(),
                .modality =
                    TGE::WindowModality::DisableParentTree
            });

        ASSERT_FALSE(root->IsInputEnabled());
        ASSERT_FALSE(parent->IsInputEnabled());
        ASSERT_FALSE(retainedBranch->IsInputEnabled());
        ASSERT_TRUE(modal->IsInputEnabled());

        std::mutex mutex;
        std::condition_variable restored;
        bool rootRestored = false;
        auto inputSubscription = root->SubscribeInputChanged(
            [&](const TGE::WindowInputChangedEvent& event)
            {
                {
                    std::scoped_lock lock(mutex);
                    rootRestored = event.enabled;
                }
                restored.notify_all();
            });

        auto detached =
            retainedBranch->EffectiveConfiguration();
        detached.parent.reset();
        detached.role = TGE::WindowRole::TopLevel;
        fake->QueueConfiguration(
            retainedBranch->Id(),
            std::move(detached));

        {
            std::unique_lock lock(mutex);
            ASSERT_TRUE(restored.wait_for(
                lock,
                std::chrono::seconds(2),
                [&]
                {
                    return rootRestored;
                }));
        }

        // The root callback is delivered while reconciliation is still
        // walking later records. Queue one facade operation behind that work
        // before inspecting every affected ancestor.
        ASSERT_TRUE(Wait(retainedBranch->SetTitleAsync(
            "Retained branch after detach")));

        EXPECT_TRUE(root->IsInputEnabled());
        EXPECT_TRUE(parent->IsInputEnabled());
        EXPECT_FALSE(retainedBranch->IsInputEnabled());
        EXPECT_TRUE(modal->IsInputEnabled());
    }

    TEST_F(
        ManagerFixture,
        ApplicationModalSuppressesWindowsCreatedWhileItIsOpen)
    {
        const auto existing = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Existing" });
        const auto modal = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Application modal",
                .role = TGE::WindowRole::Modal,
                .modality = TGE::WindowModality::ApplicationModal
            });
        const auto createdLater = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Created later" });

        EXPECT_FALSE(existing->IsInputEnabled());
        EXPECT_FALSE(createdLater->IsInputEnabled());
        EXPECT_TRUE(modal->IsInputEnabled());

        ASSERT_TRUE(Wait(manager->DestroyWindowAsync(modal->Id())));
        EXPECT_TRUE(existing->IsInputEnabled());
        EXPECT_TRUE(createdLater->IsInputEnabled());
    }

    TEST_F(
        ManagerFixture,
        NewerModalTemporarilySupersedesAnOlderApplicationModal)
    {
        const auto ordinary = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Ordinary" });
        const auto firstModal = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "First modal",
                .role = TGE::WindowRole::Modal,
                .modality = TGE::WindowModality::ApplicationModal
            });
        const auto secondModal = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Second modal",
                .role = TGE::WindowRole::Modal,
                .modality = TGE::WindowModality::ApplicationModal
            });

        EXPECT_FALSE(ordinary->IsInputEnabled());
        EXPECT_FALSE(firstModal->IsInputEnabled());
        EXPECT_TRUE(secondModal->IsInputEnabled());

        ASSERT_TRUE(Wait(
            manager->DestroyWindowAsync(secondModal->Id())));
        EXPECT_FALSE(ordinary->IsInputEnabled());
        EXPECT_TRUE(firstModal->IsInputEnabled());

        ASSERT_TRUE(Wait(
            manager->DestroyWindowAsync(firstModal->Id())));
        EXPECT_TRUE(ordinary->IsInputEnabled());
    }

    TEST_F(
        ManagerFixture,
        ResetSubscriptionPreventsFutureInvocation)
    {
        const auto window = Create(*manager);
        std::atomic<std::size_t> invocations { 0 };
        auto subscription = window->SubscribeConfigurationChanged(
            [&](const TGE::WindowConfigurationChangedEvent&)
            {
                invocations.fetch_add(1);
            });

        subscription.Reset();
        ASSERT_TRUE(Wait(window->SetTitleAsync("No callback")));
        EXPECT_EQ(invocations.load(), 0U);
    }

    TEST(
        WindowManagerLifetime,
        ManagerShutdownDestroysRetainedFacadesAndFutureCallsFailSafely)
    {
        auto eventPump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* pump = eventPump.get();
        auto runtime =
            std::make_shared<TGE::Internal::DesktopEventRuntime>(
                std::move(eventPump));
        std::jthread runner(
            [runtime](std::stop_token stopping)
            {
                (void)runtime->Run(stopping);
            });
        ASSERT_TRUE(pump->WaitUntilStarted());

        std::shared_ptr<TGE::IWindow> retained;
        std::atomic<std::size_t> closedCallbacks { 0 };
        TGE::WindowSubscription closedSubscription;
        {
            auto platform =
                std::make_unique<TGE::Tests::FakeWindowPlatform>(*pump);
            auto manager =
                std::make_unique<TGE::Internal::WindowManager>(
                    runtime,
                    std::move(platform));
            retained = Create(*manager);
            closedSubscription = retained->SubscribeClosed(
                [&](const TGE::WindowClosedEvent&)
                {
                    closedCallbacks.fetch_add(1);
                });
        }

        ASSERT_TRUE(retained);
        EXPECT_EQ(
            retained->LifecycleState(),
            TGE::WindowLifecycleState::Destroyed);

        const auto operation = Wait(retained->ShowAsync());
        ASSERT_FALSE(operation);
        EXPECT_EQ(
            operation.error().code,
            TGE::WindowErrorCode::ManagerStopped);
        EXPECT_EQ(closedCallbacks.load(), 0U);

        runtime->RequestStop();
        runner.join();
    }

    TEST(
        WindowManagerLifetime,
        OffThreadFallbackDestroysChildrenBeforePlatformShutdown)
    {
        auto eventPump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* pump = eventPump.get();
        auto runtime =
            std::make_shared<TGE::Internal::DesktopEventRuntime>(
                std::move(eventPump));
        auto lifecycle =
            std::make_shared<
                TGE::Tests::FakeWindowPlatformLifecycle>();
        auto platform =
            std::make_unique<TGE::Tests::FakeWindowPlatform>(
                *pump,
                std::shared_ptr<std::atomic<std::size_t>> {},
                lifecycle);
        auto manager =
            std::make_unique<TGE::Internal::WindowManager>(
                runtime,
                std::move(platform));

        std::jthread runner(
            [runtime](std::stop_token stopping)
            {
                (void)runtime->Run(stopping);
            });
        ASSERT_TRUE(pump->WaitUntilStarted());

        const auto parent = Create(
            *manager,
            TGE::WindowDescriptor { .title = "Parent" });
        const auto child = Create(
            *manager,
            TGE::WindowDescriptor {
                .title = "Child",
                .role = TGE::WindowRole::Child,
                .parent = parent->Id()
            });

        manager.reset();

        auto flushed = TGE::Execution::SyncWait(
            runtime->Submit(
                []
                {
                    return true;
                }));
        ASSERT_TRUE(flushed);

        {
            std::scoped_lock lock(lifecycle->mutex);
            EXPECT_EQ(
                lifecycle->destroyedWindows,
                (std::vector<TGE::WindowId> {
                    child->Id(),
                    parent->Id()
                }));
            EXPECT_EQ(lifecycle->shutdownCount, 1U);
            EXPECT_FALSE(lifecycle->shutdownWithLiveWindows);
            ASSERT_EQ(lifecycle->cleanupThreads.size(), 3U);
            EXPECT_TRUE(std::ranges::all_of(
                lifecycle->cleanupThreads,
                [pump](std::thread::id thread)
                {
                    return thread == pump->RunnerThread();
                }));
        }

        runtime->RequestStop();
        runner.join();
    }

    TEST(
        WindowManagerLifetime,
        DestructionBeforeTheDesktopRuntimeStartsNeverBlocks)
    {
        auto eventPump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* pump = eventPump.get();
        auto runtime =
            std::make_shared<TGE::Internal::DesktopEventRuntime>(
                std::move(eventPump));
        auto destructions =
            std::make_shared<std::atomic<std::size_t>>(0);
        auto platform =
            std::make_unique<TGE::Tests::FakeWindowPlatform>(
                *pump,
                destructions);

        const auto started = std::chrono::steady_clock::now();
        {
            auto manager =
                std::make_unique<TGE::Internal::WindowManager>(
                    runtime,
                    std::move(platform));
        }
        const auto elapsed =
            std::chrono::steady_clock::now() - started;

        EXPECT_LT(elapsed, std::chrono::milliseconds(100));
        EXPECT_FALSE(
            pump->WaitUntilStarted(std::chrono::milliseconds(1)));

        // Releasing the never-run runtime abandons the deferred cleanup
        // command and safely releases the adapter state.
        EXPECT_EQ(destructions->load(), 0U);
        runtime.reset();
        EXPECT_EQ(destructions->load(), 1U);
    }

    TEST(
        WindowManagerLifetime,
        PreRunOperationIsRejectedWhenManagerAndRuntimeAreReleased)
    {
        auto eventPump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* pump = eventPump.get();
        auto runtime =
            std::make_shared<TGE::Internal::DesktopEventRuntime>(
                std::move(eventPump));
        auto destructions =
            std::make_shared<std::atomic<std::size_t>>(0);
        auto platform =
            std::make_unique<TGE::Tests::FakeWindowPlatform>(
                *pump,
                destructions);
        auto manager =
            std::make_unique<TGE::Internal::WindowManager>(
                runtime,
                std::move(platform));
        auto pending = manager->CreateWindowAsync(
            TGE::WindowDescriptor { .title = "Never started" });
        std::promise<void> waiting;
        auto waitingFuture = waiting.get_future();
        auto result = std::async(
            std::launch::async,
            [
                pending = std::move(pending),
                waiting = std::move(waiting)
            ]() mutable
            {
                waiting.set_value();
                return Wait(std::move(pending));
            });
        waitingFuture.wait();
        EXPECT_EQ(
            result.wait_for(std::chrono::milliseconds(20)),
            std::future_status::timeout);

        manager.reset();
        runtime.reset();

        ASSERT_EQ(
            result.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
        const auto completion = result.get();
        ASSERT_FALSE(completion);
        EXPECT_EQ(
            completion.error().code,
            TGE::WindowErrorCode::ManagerStopped);
        EXPECT_EQ(destructions->load(), 1U);
    }

    TEST(
        WindowManagerLifetime,
        ConcurrentOperationsEitherFinishOrObserveOrderlyManagerShutdown)
    {
        auto eventPump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto* pump = eventPump.get();
        auto runtime =
            std::make_shared<TGE::Internal::DesktopEventRuntime>(
                std::move(eventPump));
        auto platform =
            std::make_unique<TGE::Tests::FakeWindowPlatform>(*pump);
        platform->SetCallDelay(std::chrono::milliseconds(3));
        auto manager =
            std::make_unique<TGE::Internal::WindowManager>(
                runtime,
                std::move(platform));

        std::jthread runner(
            [runtime](std::stop_token stopping)
            {
                (void)runtime->Run(stopping);
            });
        ASSERT_TRUE(pump->WaitUntilStarted());
        const auto retained = Create(*manager);

        constexpr std::size_t callerCount = 10;
        std::barrier start(
            static_cast<std::ptrdiff_t>(callerCount + 1));
        std::atomic<std::size_t> completed { 0 };
        std::atomic<std::size_t> stopped { 0 };
        std::atomic<std::size_t> unexpected { 0 };
        std::vector<std::thread> callers;
        callers.reserve(callerCount);

        for (std::size_t index = 0; index < callerCount; ++index)
        {
            callers.emplace_back(
                [&, index]
                {
                    start.arrive_and_wait();
                    const auto result = Wait(retained->SetTitleAsync(
                        "Pending " + std::to_string(index)));
                    if (result)
                    {
                        completed.fetch_add(1);
                    }
                    else if (result.error().code ==
                            TGE::WindowErrorCode::ManagerStopped)
                    {
                        stopped.fetch_add(1);
                    }
                    else
                    {
                        unexpected.fetch_add(1);
                    }
                });
        }

        start.arrive_and_wait();
        manager.reset();
        for (auto& caller : callers)
        {
            caller.join();
        }

        EXPECT_EQ(completed.load() + stopped.load(), callerCount);
        EXPECT_EQ(unexpected.load(), 0U);
        EXPECT_EQ(
            retained->LifecycleState(),
            TGE::WindowLifecycleState::Destroyed);

        runtime->RequestStop();
        runner.join();
    }

    TEST_F(
        ManagerFixture,
        InvalidParentAndPlatformFailuresAreReportedWithoutStoppingManager)
    {
        auto invalidChild = Wait(manager->CreateWindowAsync(
            TGE::WindowDescriptor {
                .title = "Orphan",
                .role = TGE::WindowRole::Child,
                .parent = TGE::WindowId::FromValue(1000)
            }));
        ASSERT_FALSE(invalidChild);
        EXPECT_EQ(
            invalidChild.error().code,
            TGE::WindowErrorCode::ParentNotFound);

        fake->FailNext(TGE::WindowError {
            .code = TGE::WindowErrorCode::PlatformFailure,
            .message = "Injected creation failure"
        });
        auto failed = Wait(manager->CreateWindowAsync(
            TGE::WindowDescriptor { .title = "Failure" }));
        ASSERT_FALSE(failed);
        EXPECT_EQ(
            failed.error().code,
            TGE::WindowErrorCode::PlatformFailure);

        EXPECT_TRUE(Create(*manager));
    }
}
