#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <utility>

#include "TGE/World2D.hpp"

namespace
{
    void ExpectPointNear(
        TGE::Vector2f actual,
        TGE::Vector2f expected,
        float tolerance = 1.0e-4F)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
    }

    TGE::Node2DId MustCreateNode(
        TGE::World2D& world,
        TGE::Transform2D transform = {})
    {
        auto node = world.CreateNode(transform);
        if (!node)
        {
            throw std::runtime_error(node.error().message);
        }
        return *node;
    }
}

TEST(World2DHierarchyTests, ComposesParentTransformsAndRejectsCycles)
{
    TGE::World2D world;
    const auto root = MustCreateNode(world, TGE::Transform2D {
        .translation = { 10.0F, 20.0F }
    });
    const auto child = MustCreateNode(world, TGE::Transform2D {
        .translation = { 3.0F, 4.0F }
    });

    ASSERT_TRUE(world.SetParent(child, root));
    const auto childWorld = world.WorldTransform(child);
    ASSERT_TRUE(childWorld);
    ExpectPointNear(
        childWorld->TransformPoint({}),
        { 13.0F, 24.0F });

    const auto cycle = world.SetParent(root, child);
    ASSERT_FALSE(cycle);
    EXPECT_EQ(
        cycle.error().code,
        TGE::World2DErrorCode::HierarchyCycle);
}

TEST(World2DHierarchyTests, KeepWorldReparentingPreservesPlacement)
{
    TGE::World2D world;
    const auto firstParent = MustCreateNode(world, TGE::Transform2D {
        .translation = { 10.0F, 20.0F }
    });
    const auto secondParent = MustCreateNode(world, TGE::Transform2D {
        .translation = { -30.0F, 40.0F }
    });
    const auto child = MustCreateNode(world, TGE::Transform2D {
        .translation = { 2.0F, 3.0F }
    });

    ASSERT_TRUE(world.SetParent(child, firstParent));
    const auto before = world.WorldTransform(child);
    ASSERT_TRUE(before);

    ASSERT_TRUE(world.SetParent(
        child,
        secondParent,
        TGE::ReparentMode::KeepWorld));
    const auto after = world.WorldTransform(child);
    ASSERT_TRUE(after);
    ExpectPointNear(
        after->TransformPoint({}),
        before->TransformPoint({}));

    const auto local = world.Transform(child);
    ASSERT_TRUE(local);
    ExpectPointNear(local->translation, { 42.0F, -17.0F });
}

TEST(World2DHierarchyTests, DestroySubtreeInvalidatesGenerationalIds)
{
    TGE::World2D world;
    const auto root = MustCreateNode(world);
    const auto child = MustCreateNode(world);
    ASSERT_TRUE(world.SetParent(child, root));

    const auto visual = world.AddRectangle(
        child,
        TGE::Rectangle2D { .size = { 8.0F, 6.0F } });
    ASSERT_TRUE(visual);
    ASSERT_TRUE(world.DestroyNode(root));

    EXPECT_FALSE(world.Contains(root));
    EXPECT_FALSE(world.Contains(child));
    EXPECT_FALSE(world.Contains(*visual));

    const auto replacement = MustCreateNode(world);
    EXPECT_NE(replacement, root);
    EXPECT_NE(replacement, child);
}

TEST(World2DHierarchyTests, RejectsIdsOwnedByAnotherWorld)
{
    TGE::World2D first;
    TGE::World2D second;
    const auto node = MustCreateNode(first);

    EXPECT_TRUE(first.Contains(node));
    EXPECT_FALSE(second.Contains(node));

    const auto result = second.SetTransform(node, {});
    ASSERT_FALSE(result);
    EXPECT_EQ(
        result.error().code,
        TGE::World2DErrorCode::NodeNotFound);
}

TEST(World2DHierarchyTests, ReparentsChildrenWithoutInvertingAncestors)
{
    TGE::World2D world;
    const auto singularAncestor = MustCreateNode(
        world,
        TGE::Transform2D { .scale = { 0.0F, 1.0F } });
    const auto removed = MustCreateNode(
        world,
        TGE::Transform2D { .translation = { 4.0F, 5.0F } });
    const auto child = MustCreateNode(
        world,
        TGE::Transform2D { .translation = { 2.0F, 3.0F } });
    ASSERT_TRUE(world.SetParent(removed, singularAncestor));
    ASSERT_TRUE(world.SetParent(child, removed));

    const auto before = world.WorldTransform(child);
    ASSERT_TRUE(before);
    ASSERT_TRUE(world.DestroyNode(
        removed,
        TGE::NodeDestroyPolicy::ReparentChildren));
    const auto after = world.WorldTransform(child);
    ASSERT_TRUE(after);
    EXPECT_EQ(*after, *before);
    EXPECT_TRUE(world.Contains(child));
}

TEST(World2DHierarchyTests, FailedChildReparentingIsAtomic)
{
    TGE::World2D world;
    const auto removed = MustCreateNode(
        world,
        TGE::Transform2D { .scale = { 2.0F, 1.0F } });
    const auto child = MustCreateNode(
        world,
        TGE::Transform2D {
            .rotation = TGE::Angle::FromDegrees(45.0F)
        });
    ASSERT_TRUE(world.SetParent(child, removed));
    const auto before = world.WorldTransform(child);
    ASSERT_TRUE(before);

    const auto result = world.DestroyNode(
        removed,
        TGE::NodeDestroyPolicy::ReparentChildren);
    ASSERT_FALSE(result);
    EXPECT_EQ(
        result.error().code,
        TGE::World2DErrorCode::NonDecomposableTransform);
    EXPECT_TRUE(world.Contains(removed));
    EXPECT_TRUE(world.Contains(child));
    EXPECT_EQ(world.WorldTransform(child), before);
}

TEST(World2DHierarchyTests, MovedFromWorldRemainsAnEmptyUsableWorld)
{
    TGE::World2D original;
    const auto originalNode = MustCreateNode(original);
    TGE::World2D moved(std::move(original));

    EXPECT_TRUE(moved.Contains(originalNode));
    EXPECT_FALSE(original.Contains(originalNode));
    EXPECT_EQ(original.Revision(), 0U);

    const auto replacement = original.CreateNode();
    ASSERT_TRUE(replacement);
    EXPECT_TRUE(original.Contains(*replacement));
}

TEST(World2DHierarchyTests, InvalidNodeTransformReturnsAnError)
{
    TGE::World2D world;
    const auto result = world.CreateNode(TGE::Transform2D {
        .translation = {
            std::numeric_limits<float>::infinity(),
            0.0F
        }
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(
        result.error().code,
        TGE::World2DErrorCode::InvalidDescriptor);
    EXPECT_EQ(world.Revision(), 0U);
}
