#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

#include "TGE/Drawing2D.hpp"
#include "TGE/World2D.hpp"

namespace
{
    TGE::RenderPass2D MakePass(
        TGE::View2D view = {})
    {
        auto pass = TGE::RenderPass2D::Create({
            .target = TGE::RenderTarget::FromValues(1, 1),
            .view = view
        });
        if (!pass)
        {
            throw std::runtime_error(pass.error().message);
        }
        return std::move(*pass);
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

TEST(RenderScene2DTests, PublishesOwningSnapshotsInPainterOrder)
{
    TGE::World2D world;
    const auto node = MustCreateNode(world, TGE::Transform2D {
        .translation = { 10.0F, 20.0F }
    });

    TGE::VisualProperties2D laterProperties {
        .localTransform = TGE::Transform2D {
            .translation = { 2.0F, 3.0F }
        },
        .drawOrder = { .layer = 2, .order = 1 }
    };
    TGE::VisualProperties2D earlierProperties {
        .drawOrder = { .layer = -1, .order = 9 }
    };

    const auto later = world.AddRectangle(
        node,
        TGE::Rectangle2D { .size = { 8.0F, 6.0F } },
        laterProperties);
    const auto earlier = world.AddCircle(
        node,
        TGE::Circle2D { .radius = 4.0F },
        earlierProperties);
    ASSERT_TRUE(later);
    ASSERT_TRUE(earlier);

    const auto snapshot = world.PublishRenderScene();
    ASSERT_EQ(snapshot.Items().size(), 2U);
    EXPECT_EQ(snapshot.Items()[0].visual, *earlier);
    EXPECT_EQ(snapshot.Items()[1].visual, *later);
    EXPECT_TRUE(std::holds_alternative<TGE::Circle2D>(
        snapshot.Items()[0].value));
    EXPECT_TRUE(std::holds_alternative<TGE::Rectangle2D>(
        snapshot.Items()[1].value));
    EXPECT_FLOAT_EQ(snapshot.Items()[1].worldBounds.x, 12.0F);
    EXPECT_FLOAT_EQ(snapshot.Items()[1].worldBounds.y, 23.0F);
    EXPECT_FLOAT_EQ(snapshot.Items()[1].worldBounds.width, 8.0F);
    EXPECT_FLOAT_EQ(snapshot.Items()[1].worldBounds.height, 6.0F);

    laterProperties.drawOrder = { .layer = -2, .order = 0 };
    ASSERT_TRUE(world.SetVisualProperties(*later, laterProperties));
    ASSERT_TRUE(world.RemoveVisual(*earlier));

    ASSERT_EQ(snapshot.Items().size(), 2U);
    EXPECT_EQ(snapshot.Items()[0].visual, *earlier);
    EXPECT_EQ(
        snapshot.Items()[1].drawOrder,
        (TGE::DrawOrder2D { .layer = 2, .order = 1 }));

    const auto updated = world.PublishRenderScene();
    ASSERT_EQ(updated.Items().size(), 1U);
    EXPECT_EQ(updated.Items()[0].visual, *later);
    EXPECT_EQ(
        updated.Items()[0].drawOrder,
        (TGE::DrawOrder2D { .layer = -2, .order = 0 }));
    EXPECT_LT(snapshot.Revision(), updated.Revision());
}

TEST(RenderScene2DTests, ExcludesDisabledHierarchyAndHiddenVisuals)
{
    TGE::World2D world;
    const auto parent = MustCreateNode(world);
    const auto child = MustCreateNode(world);
    ASSERT_TRUE(world.SetParent(child, parent));

    const auto visual = world.AddRectangle(
        child,
        TGE::Rectangle2D { .size = { 2.0F, 2.0F } });
    ASSERT_TRUE(visual);
    EXPECT_EQ(world.PublishRenderScene().Items().size(), 1U);

    ASSERT_TRUE(world.SetEnabled(parent, false));
    EXPECT_TRUE(world.PublishRenderScene().Empty());

    ASSERT_TRUE(world.SetEnabled(parent, true));
    auto properties = world.VisualProperties(*visual);
    ASSERT_TRUE(properties);
    properties->visible = false;
    ASSERT_TRUE(world.SetVisualProperties(*visual, *properties));
    EXPECT_TRUE(world.PublishRenderScene().Empty());
}

TEST(RenderScene2DTests, RecordsMatchingVisibleItemsAndCullsByView)
{
    TGE::World2D world;
    const auto insideNode = MustCreateNode(world);
    const auto outsideNode = MustCreateNode(world, TGE::Transform2D {
        .translation = { 1000.0F, 1000.0F }
    });
    const auto maskedNode = MustCreateNode(world, TGE::Transform2D {
        .translation = { 10.0F, 10.0F }
    });

    ASSERT_TRUE(world.AddRectangle(
        insideNode,
        TGE::Rectangle2D { .size = { 10.0F, 10.0F } },
        TGE::VisualProperties2D {
            .drawState = TGE::DrawState2D { .pixelSnapped = true }
        }));
    ASSERT_TRUE(world.AddRectangle(
        outsideNode,
        TGE::Rectangle2D { .size = { 10.0F, 10.0F } }));
    ASSERT_TRUE(world.AddRectangle(
        maskedNode,
        TGE::Rectangle2D { .size = { 10.0F, 10.0F } },
        TGE::VisualProperties2D {
            .visibilityMask = TGE::VisibilityMask2D::FromBits(0b10)
        }));

    auto pass = MakePass(TGE::View2D {
        .center = {},
        .size = { 100.0F, 100.0F }
    });
    world.PublishRenderScene().Record(
        pass,
        TGE::VisibilityMask2D::FromBits(0b01));

    ASSERT_EQ(pass.CommandCount(), 1U);
    const auto& command =
        std::get<TGE::RectangleDrawCommand2D>(pass.Commands()[0]);
    EXPECT_EQ(
        command.transform.TransformPoint({}),
        (TGE::Vector2f {}));
    EXPECT_TRUE(command.state.pixelSnapped);
}

TEST(RenderScene2DTests, RejectsInvalidOrTypeChangingVisualUpdates)
{
    TGE::World2D world;
    const auto node = MustCreateNode(world);

    const auto invalid = world.AddRectangle(node, TGE::Rectangle2D {});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(
        invalid.error().code,
        TGE::World2DErrorCode::InvalidDescriptor);

    const auto rectangle = world.AddRectangle(
        node,
        TGE::Rectangle2D { .size = { 2.0F, 3.0F } });
    ASSERT_TRUE(rectangle);

    const auto typeChange = world.SetVisual(
        *rectangle,
        TGE::Visual2D {
            std::in_place_type<TGE::Circle2D>,
            TGE::Circle2D { .radius = 2.0F }
        });
    ASSERT_FALSE(typeChange);
    EXPECT_EQ(
        typeChange.error().code,
        TGE::World2DErrorCode::VisualTypeMismatch);
}

TEST(RenderScene2DTests, RejectsMovedFromValidatedGeometry)
{
    TGE::World2D world;
    const auto node = MustCreateNode(world);
    auto geometry = TGE::Geometry2D::Create(
        {
            { .position = { 0.0F, 0.0F } },
            { .position = { 1.0F, 0.0F } },
            { .position = { 0.0F, 1.0F } }
        },
        { 0, 1, 2 });
    ASSERT_TRUE(geometry);

    auto retainedGeometry = std::move(*geometry);
    ASSERT_TRUE(world.AddGeometry(node, std::move(retainedGeometry)));
    const auto movedFrom = world.AddGeometry(node, std::move(retainedGeometry));
    ASSERT_FALSE(movedFrom);
    EXPECT_EQ(
        movedFrom.error().code,
        TGE::World2DErrorCode::InvalidDescriptor);
}

TEST(RenderScene2DTests, MovedFromSnapshotRemainsQueryable)
{
    TGE::World2D world;
    const auto node = MustCreateNode(world);
    ASSERT_TRUE(world.AddCircle(
        node,
        TGE::Circle2D { .radius = 2.0F }));

    auto original = world.PublishRenderScene();
    TGE::RenderScene2D moved(std::move(original));

    EXPECT_EQ(original.Items().size(), 1U);
    EXPECT_EQ(moved.Items().size(), 1U);
    EXPECT_EQ(original.Revision(), moved.Revision());
}

TEST(RenderScene2DTests, OverflowedHierarchyCannotBecomeAValidPass)
{
    TGE::World2D world;
    const auto parent = MustCreateNode(world, TGE::Transform2D {
        .scale = { std::numeric_limits<float>::max(), 1.0F }
    });
    const auto child = MustCreateNode(world, TGE::Transform2D {
        .scale = { 2.0F, 1.0F }
    });
    ASSERT_TRUE(world.SetParent(child, parent));
    ASSERT_TRUE(world.AddRectangle(
        child,
        TGE::Rectangle2D { .size = { 2.0F, 2.0F } }));

    auto pass = MakePass();
    world.PublishRenderScene().Record(pass);
    ASSERT_EQ(pass.CommandCount(), 1U);
    const auto validation = pass.Validate();
    ASSERT_FALSE(validation);
    EXPECT_EQ(
        validation.error().code,
        TGE::RenderingErrorCode::InvalidData);
}
