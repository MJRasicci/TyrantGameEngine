#include <atomic>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "TGE/Input.hpp"

namespace
{
    TEST(InputContracts, SubscriptionOwnsOneIdempotentReset)
    {
        std::atomic<std::size_t> resets { 0 };
        TGE::InputSubscription subscription(
            [&resets]
            {
                resets.fetch_add(1);
            });

        ASSERT_TRUE(subscription);
        TGE::InputSubscription moved(std::move(subscription));
        EXPECT_FALSE(subscription);
        EXPECT_TRUE(moved);

        moved.Reset();
        moved.Reset();
        EXPECT_FALSE(moved);
        EXPECT_EQ(resets.load(), 1U);
    }

    TEST(InputContracts, MoveAssignmentReleasesPreviousSubscription)
    {
        std::atomic<std::size_t> first { 0 };
        std::atomic<std::size_t> second { 0 };
        TGE::InputSubscription target(
            [&first]
            {
                first.fetch_add(1);
            });
        TGE::InputSubscription replacement(
            [&second]
            {
                second.fetch_add(1);
            });

        target = std::move(replacement);

        EXPECT_EQ(first.load(), 1U);
        EXPECT_EQ(second.load(), 0U);
        target.Reset();
        EXPECT_EQ(second.load(), 1U);
    }

    TEST(InputContracts, StateSnapshotRetainsAnImmutableOwnedState)
    {
        const auto context = TGE::InputContextId::FromValue(9);
        const auto timestamp =
            std::chrono::steady_clock::now();
        const auto key = TGE::PhysicalKeyCode::FromValue(4);

        const TGE::InputStateSnapshot snapshot(
            context,
            17,
            timestamp,
            TGE::InputContextConfiguration {
                .name = "Editor input",
                .enabled = true,
                .captured = true,
                .relativePointerMode = false
            },
            { key },
            TGE::InputModifiers {
                .shift = true
            },
            TGE::InputPoint { 12.5, 24.0 },
            { TGE::PointerButton::Primary },
            {
                TGE::TouchContactState {
                    .contact = 23,
                    .position = { 0.25, 0.75 },
                    .pressure = 0.5F
                }
            });

        EXPECT_EQ(snapshot.Context(), context);
        EXPECT_EQ(snapshot.Sequence(), 17U);
        EXPECT_EQ(snapshot.Timestamp(), timestamp);
        EXPECT_EQ(snapshot.Configuration().name, "Editor input");
        EXPECT_EQ(snapshot.PressedKeys(), std::vector { key });
        EXPECT_TRUE(snapshot.Modifiers().shift);
        EXPECT_EQ(
            snapshot.PointerPosition(),
            (TGE::InputPoint { 12.5, 24.0 }));
        EXPECT_EQ(
            snapshot.PressedPointerButtons(),
            std::vector { TGE::PointerButton::Primary });
        ASSERT_EQ(snapshot.Touches().size(), 1U);
        EXPECT_EQ(snapshot.Touches().front().contact, 23U);
    }
}
