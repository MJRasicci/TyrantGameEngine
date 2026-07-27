#include <atomic>
#include <barrier>
#include <chrono>
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

#include "FakeDesktopEventPump.hpp"
#include "FakeInputPlatform.hpp"
#include "Internal/Desktop/DesktopEventRuntime.hpp"
#include "Internal/Input/InputManager.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Input.hpp"

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
                "Input task completed through cancellation.");
        }
        return std::get<0>(std::move(*completion));
    }

    void Wait(TGE::Task<void> task)
    {
        auto completion =
            TGE::Execution::SyncWait(std::move(task));
        if (!completion)
        {
            throw std::runtime_error(
                "Input task completed through cancellation.");
        }
    }

    std::shared_ptr<TGE::IInputContext> Create(
        TGE::IInputManager& manager,
        TGE::InputContextDescriptor descriptor = {})
    {
        auto result =
            Wait(manager.CreateContextAsync(std::move(descriptor)));
        if (!result)
        {
            throw std::runtime_error(result.error().message);
        }
        return std::move(*result);
    }

    struct InputManagerFixture : testing::Test
    {
        void SetUp() override
        {
            Start();
        }

        void TearDown() override
        {
            Stop();
        }

        void Start(
            std::vector<TGE::InputDeviceDescriptor> initialDevices = {})
        {
            platformState =
                std::make_shared<TGE::Tests::FakeInputPlatformState>();
            auto eventPump =
                std::make_unique<TGE::Tests::FakeDesktopEventPump>();
            pump = eventPump.get();
            runtime =
                std::make_shared<TGE::Internal::DesktopEventRuntime>(
                    std::move(eventPump));

            auto platform =
                std::make_unique<TGE::Tests::FakeInputPlatform>(
                    platformState);
            fake = platform.get();
            fake->connectedDevices = std::move(initialDevices);
            manager =
                std::make_unique<TGE::Internal::InputManager>(
                    runtime,
                    std::move(platform));

            runner = std::jthread(
                [runtime = runtime]
                {
                    (void)runtime->Run();
                });
            if (!pump->WaitUntilStarted())
            {
                throw std::runtime_error(
                    "Fake desktop event runtime failed to start.");
            }
            running = true;
            Flush();
        }

        void Stop()
        {
            if (manager)
            {
                if (running)
                {
                    Wait(manager->ShutdownAsync());
                }
                manager.reset();
            }
            if (runtime && running)
            {
                runtime->RequestStop();
                runner.join();
                running = false;
            }
            runtime.reset();
            fake = nullptr;
            pump = nullptr;
        }

        void Restart(
            std::vector<TGE::InputDeviceDescriptor> initialDevices)
        {
            Stop();
            Start(std::move(initialDevices));
        }

        void Flush()
        {
            auto completion = TGE::Execution::SyncWait(
                runtime->Submit(
                    []
                    {
                        return true;
                    }));
            if (!completion)
            {
                throw std::runtime_error(
                    "Desktop event runtime flush was cancelled.");
            }
        }

        TGE::Tests::FakeDesktopEventPump* pump { nullptr };
        TGE::Tests::FakeInputPlatform* fake { nullptr };
        std::shared_ptr<TGE::Tests::FakeInputPlatformState>
            platformState;
        std::shared_ptr<TGE::Internal::DesktopEventRuntime> runtime;
        std::unique_ptr<TGE::Internal::InputManager> manager;
        std::jthread runner;
        bool running { false };
    };

    TEST_F(
        InputManagerFixture,
        CreatesIndependentContextsWithEffectiveConfigurationAndTarget)
    {
        TGE::InputContextDescriptor descriptor {
            .name = "Editor viewport",
            .initiallyEnabled = true,
            .requestCapture = true,
            .requestRelativePointerMode = true
        };
        const auto target =
            TGE::Internal::InputPlatformTarget::FromValue(42);
        auto result = Wait(manager->CreateContextForTargetAsync(
            descriptor,
            target));

        ASSERT_TRUE(result) << result.error().message;
        const auto context = std::move(*result);
        EXPECT_TRUE(context->Id());
        EXPECT_EQ(context->RequestedDescriptor(), descriptor);
        EXPECT_EQ(
            context->EffectiveConfiguration(),
            (TGE::InputContextConfiguration {
                .name = "Editor viewport",
                .enabled = true,
                .captured = true,
                .relativePointerMode = true
            }));
        EXPECT_EQ(
            context->LifecycleState(),
            TGE::InputContextLifecycleState::Open);
        EXPECT_TRUE(context->Capabilities().keyboard);
        ASSERT_TRUE(fake->TargetFor(context->Id()));
        EXPECT_EQ(*fake->TargetFor(context->Id()), target);

        const auto snapshot = context->StateSnapshot();
        ASSERT_TRUE(snapshot);
        EXPECT_EQ(snapshot->Context(), context->Id());
        EXPECT_EQ(snapshot->Sequence(), 0U);
        EXPECT_EQ(
            snapshot->Configuration(),
            context->EffectiveConfiguration());
        ASSERT_EQ(manager->Contexts().size(), 1U);
        EXPECT_EQ(manager->FindContext(context->Id()), context);
    }

    TEST_F(
        InputManagerFixture,
        UnsupportedPreferencesAreObservableAndMutationsAreResultBearing)
    {
        fake->capabilities.capture = false;
        fake->capabilities.relativePointerMode = false;
        const auto context = Create(
            *manager,
            TGE::InputContextDescriptor {
                .name = "Portable",
                .requestCapture = true,
                .requestRelativePointerMode = true
            });

        EXPECT_FALSE(context->Capabilities().capture);
        EXPECT_FALSE(context->EffectiveConfiguration().captured);
        EXPECT_FALSE(
            context->EffectiveConfiguration().relativePointerMode);

        const auto capture = Wait(context->SetCaptureAsync(true));
        ASSERT_FALSE(capture);
        EXPECT_EQ(
            capture.error().code,
            TGE::InputErrorCode::Unsupported);
        EXPECT_EQ(
            context->LifecycleState(),
            TGE::InputContextLifecycleState::Open);

        const auto enabled = Wait(context->SetEnabledAsync(false));
        ASSERT_TRUE(enabled);
        EXPECT_EQ(*enabled, TGE::InputOperationStatus::Applied);
        EXPECT_FALSE(context->EffectiveConfiguration().enabled);
    }

    TEST_F(
        InputManagerFixture,
        PlatformFailuresAndExceptionsDoNotCorruptContextState)
    {
        const auto context = Create(*manager);
        fake->FailNext(TGE::InputError {
            .code = TGE::InputErrorCode::PlatformFailure,
            .message = "expected failure"
        });

        const auto failure = Wait(context->SetEnabledAsync(false));
        ASSERT_FALSE(failure);
        EXPECT_EQ(failure.error().message, "expected failure");
        EXPECT_TRUE(context->EffectiveConfiguration().enabled);

        fake->ThrowNext();
        const auto exception = Wait(context->SetCaptureAsync(true));
        ASSERT_FALSE(exception);
        EXPECT_EQ(
            exception.error().code,
            TGE::InputErrorCode::PlatformFailure);
        EXPECT_NE(
            exception.error().message.find("fake input platform exception"),
            std::string::npos);
        EXPECT_FALSE(context->EffectiveConfiguration().captured);

        fake->FailNext(TGE::InputError {
            .code = TGE::InputErrorCode::PlatformFailure,
            .message = "cannot destroy"
        });
        const auto destruction =
            Wait(manager->DestroyContextAsync(context->Id()));
        ASSERT_FALSE(destruction);
        EXPECT_EQ(
            context->LifecycleState(),
            TGE::InputContextLifecycleState::Open);
        EXPECT_EQ(manager->FindContext(context->Id()), context);
    }

    TEST_F(
        InputManagerFixture,
        ConcurrentCallersAreSerializedOnTheDesktopEventThread)
    {
        const auto context = Create(*manager);
        {
            std::scoped_lock lock(platformState->mutex);
            platformState->callDelay = std::chrono::milliseconds(2);
        }

        constexpr std::size_t callerCount = 12;
        std::barrier start(
            static_cast<std::ptrdiff_t>(callerCount));
        std::atomic<std::size_t> succeeded { 0 };
        std::vector<std::thread> callers;
        callers.reserve(callerCount);
        for (std::size_t index = 0;
             index < callerCount;
             ++index)
        {
            callers.emplace_back(
                [&, index]
                {
                    start.arrive_and_wait();
                    const auto result = Wait(
                        context->SetCaptureAsync(index % 2 == 0));
                    if (result)
                    {
                        succeeded.fetch_add(1);
                    }
                });
        }
        for (auto& caller : callers)
        {
            caller.join();
        }

        EXPECT_EQ(succeeded.load(), callerCount);
        std::vector<TGE::Tests::FakeInputPlatformState::Call> calls;
        std::size_t maximumConcurrentCalls;
        {
            std::scoped_lock lock(platformState->mutex);
            calls = platformState->calls;
            maximumConcurrentCalls =
                platformState->maximumConcurrentCalls;
        }
        EXPECT_EQ(maximumConcurrentCalls, 1U);
        ASSERT_GE(calls.size(), callerCount + 1);
        for (const auto& call : calls)
        {
            EXPECT_EQ(call.thread, pump->RunnerThread());
        }
    }

    TEST_F(
        InputManagerFixture,
        TypedEventsShareStrictOrderAndPublishImmutableStateSnapshots)
    {
        const auto context = Create(
            *manager,
            TGE::InputContextDescriptor {
                .name = "Typed events"
            });
        const auto keyboard =
            TGE::InputDeviceId::FromValue(101);
        const auto pointer =
            TGE::InputDeviceId::FromValue(102);
        const auto touch =
            TGE::InputDeviceId::FromValue(103);
        const auto key = TGE::PhysicalKeyCode::FromValue(4);

        std::vector<std::string> kinds;
        std::vector<TGE::InputEventSequence> sequences;
        std::vector<TGE::InputTimestamp> timestamps;
        std::vector<std::thread::id> callbackThreads;
        std::shared_ptr<const TGE::InputStateSnapshot>
            keyboardSnapshot;
        auto observe =
            [&](std::string kind, const TGE::InputEventMetadata& metadata)
            {
                kinds.emplace_back(std::move(kind));
                sequences.emplace_back(metadata.sequence);
                timestamps.emplace_back(metadata.timestamp);
                callbackThreads.emplace_back(
                    std::this_thread::get_id());
            };

        auto keyboardSubscription =
            context->SubscribeKeyboard(
                [&](const TGE::KeyboardInputEvent& event)
                {
                    observe("keyboard", event.metadata);
                    keyboardSnapshot = context->StateSnapshot();
                    EXPECT_EQ(event.logicalName, "A");
                    EXPECT_TRUE(event.modifiers.shift);
                });
        auto textSubscription =
            context->SubscribeText(
                [&](const TGE::TextInputEvent& event)
                {
                    observe("text", event.metadata);
                    EXPECT_EQ(event.text, "A");
                });
        auto movedSubscription =
            context->SubscribePointerMoved(
                [&](const TGE::PointerMovedEvent& event)
                {
                    observe("moved", event.metadata);
                    EXPECT_EQ(
                        event.delta,
                        (TGE::InputDelta { 4.0, 5.0 }));
                });
        auto buttonSubscription =
            context->SubscribePointerButton(
                [&](const TGE::PointerButtonEvent& event)
                {
                    observe("button", event.metadata);
                });
        auto wheelSubscription =
            context->SubscribePointerWheel(
                [&](const TGE::PointerWheelEvent& event)
                {
                    observe("wheel", event.metadata);
                });
        auto touchSubscription =
            context->SubscribeTouch(
                [&](const TGE::TouchInputEvent& event)
                {
                    observe("touch", event.metadata);
                });

        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = context->Id(),
                .device = keyboard,
                .physicalCode = key,
                .logicalName = "A",
                .action = TGE::InputAction::Pressed,
                .modifiers = TGE::InputModifiers {
                    .shift = true
                }
            });
        fake->EmitText(
            TGE::Internal::InputPlatformTextEvent {
                .context = context->Id(),
                .device = keyboard,
                .text = "A"
            });
        fake->EmitPointerMoved(
            TGE::Internal::InputPlatformPointerMovedEvent {
                .context = context->Id(),
                .device = pointer,
                .position = { 12.0, 20.0 },
                .delta = { 4.0, 5.0 }
            });
        fake->EmitPointerButton(
            TGE::Internal::InputPlatformPointerButtonEvent {
                .context = context->Id(),
                .device = pointer,
                .button = TGE::PointerButton::Primary,
                .action = TGE::InputAction::Pressed,
                .position = { 12.0, 20.0 }
            });
        fake->EmitPointerWheel(
            TGE::Internal::InputPlatformPointerWheelEvent {
                .context = context->Id(),
                .device = pointer,
                .delta = { 0.0, -1.0 },
                .unit = TGE::InputWheelUnit::Lines
            });
        fake->EmitTouch(
            TGE::Internal::InputPlatformTouchEvent {
                .context = context->Id(),
                .device = touch,
                .contact = 7,
                .action = TGE::TouchAction::Began,
                .position = { 0.25, 0.75 },
                .pressure = 0.5F
            });
        Flush();

        EXPECT_EQ(
            kinds,
            (std::vector<std::string> {
                "keyboard",
                "text",
                "moved",
                "button",
                "wheel",
                "touch"
            }));
        EXPECT_EQ(
            sequences,
            (std::vector<TGE::InputEventSequence> {
                1, 2, 3, 4, 5, 6
            }));
        ASSERT_EQ(timestamps.size(), 6U);
        for (std::size_t index = 1;
             index < timestamps.size();
             ++index)
        {
            EXPECT_LT(timestamps[index - 1], timestamps[index]);
        }
        for (const auto thread : callbackThreads)
        {
            EXPECT_EQ(thread, pump->RunnerThread());
        }

        ASSERT_TRUE(keyboardSnapshot);
        EXPECT_EQ(keyboardSnapshot->Sequence(), 1U);
        EXPECT_EQ(
            keyboardSnapshot->PressedKeys(),
            std::vector { key });
        EXPECT_TRUE(keyboardSnapshot->Touches().empty());

        const auto current = context->StateSnapshot();
        ASSERT_TRUE(current);
        EXPECT_EQ(current->Sequence(), 6U);
        EXPECT_EQ(current->PressedKeys(), std::vector { key });
        EXPECT_TRUE(current->Modifiers().shift);
        EXPECT_EQ(
            current->PressedPointerButtons(),
            std::vector { TGE::PointerButton::Primary });
        ASSERT_EQ(current->Touches().size(), 1U);
        EXPECT_EQ(current->Touches().front().contact, 7U);

        // The event-one snapshot remains unchanged after five later events.
        EXPECT_EQ(keyboardSnapshot->Sequence(), 1U);
        EXPECT_TRUE(keyboardSnapshot->Touches().empty());
    }

    TEST_F(
        InputManagerFixture,
        RoutingAndSuppressionKeepContextStateIndependent)
    {
        const auto first = Create(
            *manager,
            TGE::InputContextDescriptor { .name = "First" });
        const auto second = Create(
            *manager,
            TGE::InputContextDescriptor { .name = "Second" });
        const auto device = TGE::InputDeviceId::FromValue(1);
        const auto key = TGE::PhysicalKeyCode::FromValue(30);
        std::size_t firstEvents = 0;
        std::size_t secondEvents = 0;

        auto firstSubscription =
            first->SubscribeKeyboard(
                [&](const TGE::KeyboardInputEvent&)
                {
                    ++firstEvents;
                });
        auto secondSubscription =
            second->SubscribeKeyboard(
                [&](const TGE::KeyboardInputEvent&)
                {
                    ++secondEvents;
                });

        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = first->Id(),
                .device = device,
                .physicalCode = key,
                .logicalName = "1",
                .action = TGE::InputAction::Pressed
            });
        Flush();
        const auto retained = first->StateSnapshot();
        ASSERT_EQ(firstEvents, 1U);
        EXPECT_EQ(secondEvents, 0U);
        EXPECT_EQ(retained->PressedKeys(), std::vector { key });

        ASSERT_TRUE(Wait(first->SetEnabledAsync(false)));
        EXPECT_TRUE(first->StateSnapshot()->PressedKeys().empty());
        EXPECT_EQ(retained->PressedKeys(), std::vector { key });

        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = first->Id(),
                .device = device,
                .physicalCode = key,
                .logicalName = "1",
                .action = TGE::InputAction::Released
            });
        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = second->Id(),
                .device = device,
                .physicalCode = key,
                .logicalName = "1",
                .action = TGE::InputAction::Pressed
            });
        Flush();

        EXPECT_EQ(firstEvents, 1U);
        EXPECT_EQ(secondEvents, 1U);
        EXPECT_TRUE(first->StateSnapshot()->PressedKeys().empty());
        EXPECT_EQ(
            second->StateSnapshot()->PressedKeys(),
            std::vector { key });
    }

    TEST_F(
        InputManagerFixture,
        UnmappedPhysicalKeysArePublishedWithoutCorruptingPressedState)
    {
        const auto context = Create(*manager);
        const auto device = TGE::InputDeviceId::FromValue(1);
        std::vector<TGE::KeyboardInputEvent> events;
        auto subscription =
            context->SubscribeKeyboard(
                [&events](const TGE::KeyboardInputEvent& event)
                {
                    events.emplace_back(event);
                });

        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = context->Id(),
                .device = device,
                .physicalCode = {},
                .logicalName = "MediaPlay",
                .action = TGE::InputAction::Pressed
            });
        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = context->Id(),
                .device = device,
                .physicalCode = {},
                .logicalName = "MediaStop",
                .action = TGE::InputAction::Released
            });
        Flush();

        ASSERT_EQ(events.size(), 2U);
        EXPECT_FALSE(events[0].physicalCode);
        EXPECT_FALSE(events[1].physicalCode);
        EXPECT_EQ(events[0].logicalName, "MediaPlay");
        EXPECT_EQ(events[1].logicalName, "MediaStop");
        EXPECT_TRUE(context->StateSnapshot()->PressedKeys().empty());
    }

    TEST_F(
        InputManagerFixture,
        TargetSuppressionClearsTransientStateWithoutDisablingTheContext)
    {
        const auto context = Create(*manager);
        const auto device = TGE::InputDeviceId::FromValue(1);
        const auto key = TGE::PhysicalKeyCode::FromValue(4);
        std::size_t deliveries = 0;
        auto subscription =
            context->SubscribeKeyboard(
                [&deliveries](const TGE::KeyboardInputEvent&)
                {
                    ++deliveries;
                });

        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = context->Id(),
                .device = device,
                .physicalCode = key,
                .logicalName = "A",
                .action = TGE::InputAction::Pressed,
                .modifiers = TGE::InputModifiers { .shift = true }
            });
        Flush();
        ASSERT_EQ(
            context->StateSnapshot()->PressedKeys(),
            std::vector { key });

        auto suppressedConfiguration =
            context->EffectiveConfiguration();
        suppressedConfiguration.captured = false;
        suppressedConfiguration.relativePointerMode = false;
        fake->EmitTargetInputChanged(
            context->Id(),
            suppressedConfiguration,
            true);
        Flush();

        const auto suppressed = context->StateSnapshot();
        EXPECT_TRUE(suppressed->PressedKeys().empty());
        EXPECT_EQ(suppressed->Modifiers(), TGE::InputModifiers {});
        EXPECT_TRUE(suppressed->Configuration().enabled);
        EXPECT_EQ(deliveries, 1U);

        auto restoredConfiguration =
            suppressed->Configuration();
        restoredConfiguration.captured = true;
        fake->EmitTargetInputChanged(
            context->Id(),
            restoredConfiguration,
            false);
        Flush();
        EXPECT_TRUE(
            context->EffectiveConfiguration().captured);

        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = context->Id(),
                .device = device,
                .physicalCode = key,
                .logicalName = "A",
                .action = TGE::InputAction::Pressed
            });
        Flush();

        EXPECT_EQ(deliveries, 2U);
        EXPECT_EQ(
            context->StateSnapshot()->PressedKeys(),
            std::vector { key });
    }

    TEST_F(
        InputManagerFixture,
        ResetSubscriptionSuppressesAlreadyQueuedAndLaterDeliveries)
    {
        const auto context = Create(*manager);
        std::atomic<std::size_t> deliveries { 0 };
        auto subscription =
            context->SubscribePointerButton(
                [&](const TGE::PointerButtonEvent&)
                {
                    deliveries.fetch_add(1);
                });

        auto blockerEntered =
            std::make_shared<std::promise<void>>();
        auto blockerEnteredFuture = blockerEntered->get_future();
        std::promise<void> releaseBlocker;
        auto releaseBlockerFuture =
            releaseBlocker.get_future().share();
        const auto blockerPosted = runtime->Post(
            [
                blockerEntered,
                releaseBlockerFuture
            ]() mutable
            {
                blockerEntered->set_value();
                releaseBlockerFuture.wait();
            });
        if (!blockerPosted)
        {
            releaseBlocker.set_value();
        }
        ASSERT_TRUE(blockerPosted);
        const auto blockerStatus =
            blockerEnteredFuture.wait_for(std::chrono::seconds(2));
        if (blockerStatus != std::future_status::ready)
        {
            releaseBlocker.set_value();
        }
        ASSERT_EQ(blockerStatus, std::future_status::ready);

        fake->EmitPointerButton(
            TGE::Internal::InputPlatformPointerButtonEvent {
                .context = context->Id(),
                .device = TGE::InputDeviceId::FromValue(2),
                .button = TGE::PointerButton::Primary,
                .action = TGE::InputAction::Pressed
            });
        subscription.Reset();
        releaseBlocker.set_value();
        Flush();
        EXPECT_EQ(deliveries.load(), 0U);

        subscription =
            context->SubscribePointerButton(
                [&](const TGE::PointerButtonEvent&)
                {
                    deliveries.fetch_add(1);
                });
        fake->EmitPointerButton(
            TGE::Internal::InputPlatformPointerButtonEvent {
                .context = context->Id(),
                .device = TGE::InputDeviceId::FromValue(2),
                .button = TGE::PointerButton::Primary,
                .action = TGE::InputAction::Released
            });
        Flush();
        EXPECT_EQ(deliveries.load(), 1U);

        subscription.Reset();
        fake->EmitPointerButton(
            TGE::Internal::InputPlatformPointerButtonEvent {
                .context = context->Id(),
                .device = TGE::InputDeviceId::FromValue(2),
                .button = TGE::PointerButton::Primary,
                .action = TGE::InputAction::Pressed
            });
        Flush();
        EXPECT_EQ(deliveries.load(), 1U);
    }

    TEST_F(
        InputManagerFixture,
        InventoryAndHotplugAreOwnedOrderedAndNormalized)
    {
        const auto keyboardId =
            TGE::InputDeviceId::FromValue(11);
        const auto pointerId =
            TGE::InputDeviceId::FromValue(12);
        Restart(
            {
                TGE::InputDeviceDescriptor {
                    .id = keyboardId,
                    .kind = TGE::InputDeviceKind::Keyboard,
                    .name = "Initial keyboard"
                }
            });

        ASSERT_EQ(manager->Devices().size(), 1U);
        EXPECT_EQ(manager->Devices().front().id, keyboardId);

        std::vector<TGE::InputDeviceChangedEvent> changes;
        auto subscription =
            manager->SubscribeDeviceChanged(
                [&](const TGE::InputDeviceChangedEvent& event)
                {
                    changes.emplace_back(event);
                });

        fake->EmitDeviceChanged(
            TGE::InputDeviceChangeKind::Connected,
            TGE::InputDeviceDescriptor {
                .id = keyboardId,
                .kind = TGE::InputDeviceKind::Keyboard,
                .name = "Initial keyboard"
            });
        fake->EmitDeviceChanged(
            TGE::InputDeviceChangeKind::Connected,
            TGE::InputDeviceDescriptor {
                .id = keyboardId,
                .kind = TGE::InputDeviceKind::Keyboard,
                .name = "Updated keyboard"
            });
        fake->EmitDeviceChanged(
            TGE::InputDeviceChangeKind::Updated,
            TGE::InputDeviceDescriptor {
                .id = pointerId,
                .kind = TGE::InputDeviceKind::Pointer,
                .name = "New pointer"
            });
        fake->EmitDeviceChanged(
            TGE::InputDeviceChangeKind::Disconnected,
            TGE::InputDeviceDescriptor {
                .id = keyboardId
            });
        fake->EmitDeviceChanged(
            TGE::InputDeviceChangeKind::Disconnected,
            TGE::InputDeviceDescriptor {
                .id = keyboardId
            });
        Flush();

        ASSERT_EQ(changes.size(), 3U);
        EXPECT_EQ(
            changes[0].change,
            TGE::InputDeviceChangeKind::Updated);
        EXPECT_EQ(
            changes[1].change,
            TGE::InputDeviceChangeKind::Connected);
        EXPECT_EQ(
            changes[2].change,
            TGE::InputDeviceChangeKind::Disconnected);
        EXPECT_EQ(changes[2].device.name, "Updated keyboard");
        EXPECT_EQ(changes[0].sequence, 1U);
        EXPECT_EQ(changes[1].sequence, 2U);
        EXPECT_EQ(changes[2].sequence, 3U);
        EXPECT_LT(changes[0].timestamp, changes[1].timestamp);
        EXPECT_LT(changes[1].timestamp, changes[2].timestamp);

        const auto devices = manager->Devices();
        ASSERT_EQ(devices.size(), 1U);
        EXPECT_EQ(devices.front().id, pointerId);
        EXPECT_EQ(devices.front().name, "New pointer");
    }

    TEST_F(
        InputManagerFixture,
        DeviceAndContextEventsShareOneManagerWideSequence)
    {
        const auto context = Create(*manager);
        std::vector<TGE::InputEventSequence> sequence;
        auto keyboardSubscription =
            context->SubscribeKeyboard(
                [&](const TGE::KeyboardInputEvent& event)
                {
                    sequence.emplace_back(event.metadata.sequence);
                });
        auto deviceSubscription =
            manager->SubscribeDeviceChanged(
                [&](const TGE::InputDeviceChangedEvent& event)
                {
                    sequence.emplace_back(event.sequence);
                });
        auto textSubscription =
            context->SubscribeText(
                [&](const TGE::TextInputEvent& event)
                {
                    sequence.emplace_back(event.metadata.sequence);
                });

        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = context->Id(),
                .device = TGE::InputDeviceId::FromValue(1),
                .physicalCode = TGE::PhysicalKeyCode::FromValue(4),
                .logicalName = "A"
            });
        fake->EmitDeviceChanged(
            TGE::InputDeviceChangeKind::Connected,
            TGE::InputDeviceDescriptor {
                .id = TGE::InputDeviceId::FromValue(2),
                .kind = TGE::InputDeviceKind::Pointer,
                .name = "Pointer"
            });
        fake->EmitText(
            TGE::Internal::InputPlatformTextEvent {
                .context = context->Id(),
                .device = TGE::InputDeviceId::FromValue(1),
                .text = "A"
            });
        Flush();

        EXPECT_EQ(
            sequence,
            (std::vector<TGE::InputEventSequence> { 1, 2, 3 }));
    }

    TEST_F(
        InputManagerFixture,
        ExplicitShutdownIsAwaitableAndIdempotent)
    {
        const auto context = Create(*manager);

        Wait(manager->ShutdownAsync());
        Wait(manager->ShutdownAsync());

        EXPECT_EQ(
            context->LifecycleState(),
            TGE::InputContextLifecycleState::Destroyed);
        EXPECT_TRUE(manager->Contexts().empty());
        const auto creation =
            Wait(manager->CreateContextAsync({}));
        ASSERT_FALSE(creation);
        EXPECT_EQ(
            creation.error().code,
            TGE::InputErrorCode::ManagerStopped);

        std::size_t shutdownCount;
        {
            std::scoped_lock lock(platformState->mutex);
            shutdownCount = platformState->shutdownCount;
        }
        EXPECT_EQ(shutdownCount, 1U);
    }

    TEST_F(
        InputManagerFixture,
        TeardownDetachesContextsAndCleansThePlatformInReverseOrder)
    {
        const auto first = Create(*manager);
        const auto second = Create(*manager);
        const auto third = Create(*manager);
        const auto key = TGE::PhysicalKeyCode::FromValue(4);
        auto subscription =
            first->SubscribeKeyboard(
                [](const TGE::KeyboardInputEvent&)
                {
                });
        fake->EmitKeyboard(
            TGE::Internal::InputPlatformKeyboardEvent {
                .context = first->Id(),
                .device = TGE::InputDeviceId::FromValue(1),
                .physicalCode = key,
                .logicalName = "A",
                .action = TGE::InputAction::Pressed
            });
        Flush();
        ASSERT_FALSE(first->StateSnapshot()->PressedKeys().empty());

        manager.reset();

        EXPECT_EQ(
            first->LifecycleState(),
            TGE::InputContextLifecycleState::Destroyed);
        EXPECT_EQ(
            second->LifecycleState(),
            TGE::InputContextLifecycleState::Destroyed);
        EXPECT_EQ(
            third->LifecycleState(),
            TGE::InputContextLifecycleState::Destroyed);
        EXPECT_TRUE(first->StateSnapshot()->PressedKeys().empty());
        subscription.Reset();
        EXPECT_FALSE(subscription);

        const auto mutation = Wait(first->SetEnabledAsync(false));
        ASSERT_FALSE(mutation);
        EXPECT_EQ(
            mutation.error().code,
            TGE::InputErrorCode::ManagerStopped);

        Flush();
        std::vector<TGE::InputContextId> destroyed;
        std::size_t shutdownCount;
        {
            std::scoped_lock lock(platformState->mutex);
            destroyed = platformState->destroyed;
            shutdownCount = platformState->shutdownCount;
        }
        EXPECT_EQ(
            destroyed,
            (std::vector<TGE::InputContextId> {
                third->Id(),
                second->Id(),
                first->Id()
            }));
        EXPECT_EQ(shutdownCount, 1U);
    }

    TEST_F(
        InputManagerFixture,
        StoppedRuntimeRejectsOperationsAndManagerDestructionDoesNotBlock)
    {
        const auto context = Create(*manager);
        runtime->RequestStop();
        runner.join();
        running = false;

        const auto mutation = Wait(context->SetEnabledAsync(false));
        ASSERT_FALSE(mutation);
        EXPECT_EQ(
            mutation.error().code,
            TGE::InputErrorCode::ManagerStopped);

        const auto began = std::chrono::steady_clock::now();
        manager.reset();
        const auto elapsed =
            std::chrono::steady_clock::now() - began;
        EXPECT_LT(elapsed, std::chrono::seconds(1));
        EXPECT_EQ(
            context->LifecycleState(),
            TGE::InputContextLifecycleState::Destroyed);
    }

    TEST_F(
        InputManagerFixture,
        DeferredCreationRetainsManagerStateUntilTheTaskStarts)
    {
        auto pending = manager->CreateContextAsync(
            TGE::InputContextDescriptor {
                .name = "Deferred creation"
            });

        manager.reset();

        const auto result = Wait(std::move(pending));
        ASSERT_FALSE(result);
        EXPECT_EQ(
            result.error().code,
            TGE::InputErrorCode::ManagerStopped);
    }

    TEST_F(
        InputManagerFixture,
        DeferredDestructionRetainsManagerStateUntilTheTaskStarts)
    {
        const auto context = Create(*manager);
        auto pending =
            manager->DestroyContextAsync(context->Id());

        manager.reset();

        const auto result = Wait(std::move(pending));
        ASSERT_FALSE(result);
        EXPECT_EQ(
            result.error().code,
            TGE::InputErrorCode::ManagerStopped);
        EXPECT_EQ(
            context->LifecycleState(),
            TGE::InputContextLifecycleState::Destroyed);
    }

    TEST_F(
        InputManagerFixture,
        DeferredMutationRetainsContextAndManagerStateUntilTheTaskStarts)
    {
        auto context = Create(*manager);
        auto pending = context->SetEnabledAsync(false);

        context.reset();
        manager.reset();

        const auto result = Wait(std::move(pending));
        ASSERT_FALSE(result);
        EXPECT_EQ(
            result.error().code,
            TGE::InputErrorCode::ManagerStopped);
    }

    TEST(
        InputManagerLifetime,
        PreRunOperationIsRejectedWhenManagerAndRuntimeAreReleased)
    {
        auto platformState =
            std::make_shared<TGE::Tests::FakeInputPlatformState>();
        auto eventPump =
            std::make_unique<TGE::Tests::FakeDesktopEventPump>();
        auto runtime =
            std::make_shared<TGE::Internal::DesktopEventRuntime>(
                std::move(eventPump));
        auto platform =
            std::make_unique<TGE::Tests::FakeInputPlatform>(
                platformState);
        auto manager =
            std::make_unique<TGE::Internal::InputManager>(
                runtime,
                std::move(platform));
        auto pending = manager->CreateContextAsync(
            TGE::InputContextDescriptor {
                .name = "Never started"
            });
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
            TGE::InputErrorCode::ManagerStopped);
        std::scoped_lock lock(platformState->mutex);
        EXPECT_EQ(platformState->destructionCount, 1U);
    }
}
