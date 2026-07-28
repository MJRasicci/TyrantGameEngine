#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "TGE/Drawing2D.hpp"

static_assert(std::is_abstract_v<TGE::IRenderer2D>);
static_assert(std::is_abstract_v<TGE::IRenderingDevice>);
static_assert(std::is_abstract_v<TGE::ITextService2D>);

namespace
{
    constexpr float Tolerance = 1.0e-4F;

    void ExpectNear(TGE::Vector2f actual, TGE::Vector2f expected)
    {
        EXPECT_NEAR(actual.x, expected.x, Tolerance);
        EXPECT_NEAR(actual.y, expected.y, Tolerance);
    }

    TGE::ShapeStyle2D FilledStyle()
    {
        return {
            .fill = TGE::LinearColor::White(),
            .fillEnabled = true
        };
    }

    TGE::Text2D ValidText()
    {
        auto text = TGE::Text2D::Create(
            "Tyrant",
            TGE::TextStyle2D {
                .font = TGE::Font::FromValues(4, 7),
                .fontSize = 20.0F
            },
            TGE::TextLayout2D {
                .glyphs = {
                    {
                        .font = TGE::Font::FromValues(4, 7),
                        .metrics = {
                            .size = { 12.0F, 20.0F }
                        },
                        .image = TGE::GlyphImage2D {
                            .texture =
                                TGE::TextureView2D::FromValues(4, 8),
                            .texelBounds = { .extent = { 12, 20 } }
                        }
                    }
                },
                .bounds = { 0.0F, -4.0F, 60.0F, 24.0F },
                .ready = true
            });
        EXPECT_TRUE(text);
        return std::move(*text);
    }

    TGE::ConvexPolygon2D ValidPolygon()
    {
        const std::vector<TGE::Vector2f> clockwise {
            { 0.0F, 0.0F },
            { 0.0F, 2.0F },
            { 3.0F, 2.0F },
            { 3.0F, 0.0F }
        };
        auto polygon = TGE::ConvexPolygon2D::Create(
            clockwise,
            FilledStyle());
        EXPECT_TRUE(polygon);
        return std::move(*polygon);
    }

    TGE::Geometry2D ValidGeometry()
    {
        auto geometry = TGE::Geometry2D::Create(
            {
                { .position = { 0.0F, 0.0F } },
                { .position = { 3.0F, 0.0F } },
                { .position = { 0.0F, 2.0F } }
            },
            { 0, 1, 2 });
        EXPECT_TRUE(geometry);
        return std::move(*geometry);
    }

    struct CompoundDrawable
    {
        TGE::Rectangle2D first;
        TGE::Rectangle2D second;

        void Record(
            TGE::RenderPass2D& pass,
            const TGE::AffineTransform2D& transform,
            const TGE::DrawState2D& state) const
        {
            pass.Draw(first, transform, state);
            pass.Draw(
                second,
                transform *
                    TGE::AffineTransform2D::Translation({ 4.0F, 0.0F }),
                state);
        }
    };

    static_assert(TGE::Drawable2D<CompoundDrawable>);
}

TEST(Drawing2DTypesTests, VectorsAndRectanglesProvidePortableGeometry)
{
    const TGE::Vector2f vector { 3.0F, 4.0F };
    EXPECT_FLOAT_EQ(vector.LengthSquared(), 25.0F);
    EXPECT_FLOAT_EQ(vector.Length(), 5.0F);

    const auto normalized = vector.Normalized();
    ASSERT_TRUE(normalized);
    ExpectNear(*normalized, { 0.6F, 0.8F });
    EXPECT_FALSE(TGE::Vector2f {}.Normalized());

    const TGE::FloatRect negativeExtent {
        4.0F,
        3.0F,
        -4.0F,
        -2.0F
    };
    EXPECT_EQ(
        negativeExtent.Normalized(),
        (TGE::FloatRect { 0.0F, 1.0F, 4.0F, 2.0F }));
    EXPECT_TRUE(negativeExtent.Contains({ 2.0F, 2.0F }));
}

TEST(Drawing2DTransformTests, AffineCompositionAppliesRightHandSideFirst)
{
    const auto translation =
        TGE::AffineTransform2D::Translation({ 10.0F, -2.0F });
    const auto rotation =
        TGE::AffineTransform2D::Rotation(TGE::Angle::FromDegrees(90.0F));
    const auto scale =
        TGE::AffineTransform2D::Scale({ 2.0F, 3.0F });

    const auto composed = translation * rotation * scale;
    ExpectNear(composed.TransformPoint({ 1.0F, 1.0F }), { 7.0F, 0.0F });
    EXPECT_FLOAT_EQ(composed.At(0, 2), 10.0F);
    EXPECT_THROW((void)composed.At(2, 0), std::out_of_range);
}

TEST(Drawing2DTransformTests, InversePreservesExactComposedAffineTransform)
{
    const TGE::Transform2D parent {
        .translation = { 4.0F, -3.0F },
        .rotation = TGE::Angle::FromDegrees(28.0F),
        .scale = { 2.0F, 0.75F }
    };
    const TGE::Transform2D child {
        .translation = { -2.0F, 5.0F },
        .rotation = TGE::Angle::FromDegrees(-41.0F),
        .scale = { 0.5F, 3.0F }
    };

    const auto world = parent.Matrix() * child.Matrix();
    const auto inverse = world.Inverse();
    ASSERT_TRUE(inverse);

    const TGE::Vector2f point { 8.0F, -7.0F };
    ExpectNear(
        inverse->TransformPoint(world.TransformPoint(point)),
        point);
    EXPECT_FALSE(
        TGE::AffineTransform2D::Scale({ 0.0F, 1.0F }).Inverse());
}

TEST(Drawing2DTransformTests, TransformBoundsIncludesEveryRotatedCorner)
{
    const auto transform =
        TGE::AffineTransform2D::Rotation(
            TGE::Angle::FromDegrees(90.0F));
    const auto bounds =
        transform.TransformBounds({ 0.0F, 0.0F, 4.0F, 2.0F });

    EXPECT_NEAR(bounds.x, -2.0F, Tolerance);
    EXPECT_NEAR(bounds.y, 0.0F, Tolerance);
    EXPECT_NEAR(bounds.width, 2.0F, Tolerance);
    EXPECT_NEAR(bounds.height, 4.0F, Tolerance);
}

TEST(Drawing2DTransformTests, OriginProvidesAUniformDrawablePivot)
{
    const TGE::Transform2D transform {
        .translation = { 10.0F, 20.0F },
        .rotation = TGE::Angle::FromDegrees(90.0F),
        .scale = { 2.0F, 2.0F },
        .origin = { 3.0F, 4.0F }
    };

    const auto pivot = transform.Matrix().TransformPoint(transform.origin);
    EXPECT_NEAR(pivot.x, transform.translation.x, 1.0e-5F);
    EXPECT_NEAR(pivot.y, transform.translation.y, 1.0e-5F);
}

TEST(Drawing2DViewTests, WorldAndPixelMappingRoundTrip)
{
    const TGE::View2D view {
        .center = { 10.0F, 20.0F },
        .size = { 200.0F, 100.0F },
        .rotation = TGE::Angle::FromDegrees(15.0F)
    };
    const TGE::PixelRect viewport {
        .position = { 100, 50 },
        .extent = { 800, 400 }
    };

    const auto centerPixel =
        view.MapWorldToPixel(view.center, viewport);
    ASSERT_TRUE(centerPixel);
    ExpectNear(*centerPixel, { 500.0F, 250.0F });

    const TGE::Vector2f world { 42.0F, 37.0F };
    const auto pixel = view.MapWorldToPixel(world, viewport);
    ASSERT_TRUE(pixel);
    const auto roundTrip = view.MapPixelToWorld(*pixel, viewport);
    ASSERT_TRUE(roundTrip);
    ExpectNear(*roundTrip, world);
}

TEST(Drawing2DViewTests, NormalizedViewportResolvesAgainstTargetExtent)
{
    const TGE::Viewport2D viewport {
        .normalized = { 0.25F, 0.5F, 0.5F, 0.5F }
    };
    const auto resolved = viewport.Resolve({ 1000, 600 });

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->position, (TGE::PixelPoint { 250, 300 }));
    EXPECT_EQ(resolved->extent, (TGE::Extent2U { 500, 300 }));
    EXPECT_FALSE((
        TGE::Viewport2D {
            .normalized = { 0.75F, 0.0F, 0.5F, 1.0F }
        }.Resolve({ 1000, 600 })));
}

TEST(Drawing2DSpriteTests, LocalBoundsAccountForOrigin)
{
    const TGE::Sprite2D sprite {
        .region = {
            .texture = TGE::TextureView2D::FromValues(3, 9),
            .texelBounds = {
                .position = { 8, 16 },
                .extent = { 32, 24 }
            }
        },
        .size = { 4.0F, 3.0F },
        .origin = { 2.0F, 1.0F }
    };

    EXPECT_TRUE(sprite.IsValid());
    EXPECT_EQ(
        sprite.LocalBounds(),
        (TGE::FloatRect { -2.0F, -1.0F, 4.0F, 3.0F }));
}

TEST(Drawing2DTextTests, RejectsMalformedUtf8WithoutPartialMutation)
{
    EXPECT_TRUE(TGE::IsValidUtf8("Tyrant \xF0\x9F\x91\x91"));
    EXPECT_FALSE(TGE::IsValidUtf8(std::string("\xC0\xAF", 2)));
    EXPECT_FALSE(TGE::IsValidUtf8(std::string("\xED\xA0\x80", 3)));
    EXPECT_FALSE(TGE::IsValidUtf8(std::string("\xF4\x90\x80\x80", 4)));

    auto text = ValidText();
    const auto original = text.Utf8();
    const auto mutation = text.SetUtf8(std::string("\xE2\x82", 2));
    ASSERT_FALSE(mutation);
    EXPECT_EQ(mutation.error().code, TGE::Text2DErrorCode::InvalidUtf8);
    EXPECT_EQ(text.Utf8(), original);
}

TEST(Drawing2DTextTests, ValidatesShapedGlyphClustersAtUtf8Boundaries)
{
    const std::string utf8 = "A\xC3\xA9";
    TGE::TextLayout2D layout {
        .glyphs = {
            {
                .font = TGE::Font::FromValues(4, 7),
                .glyphIndex = 11,
                .cluster = 2,
                .position = { 10.0F, 0.0F }
            }
        },
        .bounds = { 0.0F, -2.0F, 20.0F, 12.0F }
    };
    const TGE::TextStyle2D style {
        .font = TGE::Font::FromValues(4, 7)
    };

    EXPECT_FALSE(TGE::Text2D::Create(utf8, style, layout));
    layout.glyphs.front().cluster = 1;
    const auto text = TGE::Text2D::Create(utf8, style, layout);
    ASSERT_TRUE(text);
    EXPECT_EQ(text->LocalBounds(), layout.bounds);
}

TEST(Drawing2DShapeTests, ConvexPolygonNormalizesWindingAndBoundsStroke)
{
    auto style = FilledStyle();
    style.stroke = TGE::StrokeStyle2D { .width = 2.0F };
    const std::vector<TGE::Vector2f> clockwise {
        { 0.0F, 0.0F },
        { 0.0F, 2.0F },
        { 3.0F, 2.0F },
        { 3.0F, 0.0F }
    };

    const auto polygon = TGE::ConvexPolygon2D::Create(clockwise, style);
    ASSERT_TRUE(polygon);
    EXPECT_GT(
        TGE::Cross(
            polygon->Points()[1] - polygon->Points()[0],
            polygon->Points()[2] - polygon->Points()[1]),
        0.0F);
    EXPECT_EQ(
        polygon->LocalBounds(),
        (TGE::FloatRect { -4.0F, -4.0F, 11.0F, 10.0F }));
}

TEST(Drawing2DShapeTests, ConvexPolygonRejectsInvalidPerimeters)
{
    const std::vector<TGE::Vector2f> concave {
        { 0.0F, 0.0F },
        { 3.0F, 0.0F },
        { 1.0F, 1.0F },
        { 3.0F, 3.0F },
        { 0.0F, 3.0F }
    };
    const auto result =
        TGE::ConvexPolygon2D::Create(concave, FilledStyle());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, TGE::Shape2DErrorCode::NonConvex);
}

TEST(Drawing2DGeometryTests, ValidatesIndexRangeAndTopologyCount)
{
    const std::vector<TGE::Vertex2D> vertices {
        { .position = { 0.0F, 0.0F } },
        { .position = { 1.0F, 0.0F } },
        { .position = { 0.0F, 1.0F } }
    };

    auto badIndex = TGE::Geometry2D::Create(vertices, { 0, 1, 3 });
    ASSERT_FALSE(badIndex);
    EXPECT_EQ(
        badIndex.error().code,
        TGE::Geometry2DErrorCode::IndexOutOfRange);

    auto badCount = TGE::Geometry2D::Create(
        vertices,
        { 0, 1 },
        TGE::PrimitiveTopology::TriangleList);
    ASSERT_FALSE(badCount);
    EXPECT_EQ(
        badCount.error().code,
        TGE::Geometry2DErrorCode::InvalidIndexCount);

    const auto valid = TGE::Geometry2D::Create(vertices, { 0, 1, 2 });
    ASSERT_TRUE(valid);
    EXPECT_EQ(
        valid->LocalBounds(),
        (TGE::FloatRect { 0.0F, 0.0F, 1.0F, 1.0F }));
}

TEST(Drawing2DRenderPassTests, RecordsOwningTypedCommandsInCallOrder)
{
    auto passResult = TGE::RenderPass2D::Create({
        .target = TGE::RenderTarget::FromValues(4, 12),
        .view = {
            .center = { 0.0F, 0.0F },
            .size = { 100.0F, 100.0F }
        },
        .label = "Drawing2D test"
    });
    ASSERT_TRUE(passResult);
    auto pass = std::move(*passResult);

    TGE::Sprite2D sprite {
        .region = {
            .texture = TGE::TextureView2D::FromValues(4, 20),
            .texelBounds = {
                .position = { 0, 0 },
                .extent = { 16, 16 }
            }
        },
        .size = { 1.0F, 1.0F }
    };
    const TGE::Transform2D decomposed {
        .translation = { 3.0F, 7.0F }
    };

    pass.Draw(sprite, decomposed);
    sprite.size = { 99.0F, 99.0F };
    pass.Draw(
        TGE::Circle2D {
            .radius = 2.0F,
            .style = FilledStyle()
        },
        TGE::AffineTransform2D::Translation({ -2.0F, 4.0F }));

    ASSERT_EQ(pass.CommandCount(), 2U);
    ASSERT_TRUE(
        std::holds_alternative<TGE::SpriteDrawCommand2D>(
            pass.Commands()[0]));
    const auto& recordedSprite =
        std::get<TGE::SpriteDrawCommand2D>(pass.Commands()[0]);
    EXPECT_EQ(recordedSprite.drawable.size, (TGE::Vector2f { 1.0F, 1.0F }));
    ExpectNear(
        recordedSprite.transform.TransformPoint({}),
        { 3.0F, 7.0F });
    EXPECT_TRUE(
        std::holds_alternative<TGE::CircleDrawCommand2D>(
            pass.Commands()[1]));
    EXPECT_TRUE(pass.Validate());
}

TEST(Drawing2DRenderPassTests, CustomDrawableExpandsIntoBuiltinCommands)
{
    auto passResult = TGE::RenderPass2D::Create({
        .target = TGE::RenderTarget::FromValues(8, 2)
    });
    ASSERT_TRUE(passResult);
    auto pass = std::move(*passResult);

    const CompoundDrawable drawable {
        .first = {
            .size = { 2.0F, 2.0F },
            .style = FilledStyle()
        },
        .second = {
            .size = { 3.0F, 1.0F },
            .style = FilledStyle()
        }
    };
    pass.Draw(
        drawable,
        TGE::AffineTransform2D::Translation({ 10.0F, 20.0F }));

    ASSERT_EQ(pass.CommandCount(), 2U);
    const auto& second =
        std::get<TGE::RectangleDrawCommand2D>(pass.Commands()[1]);
    ExpectNear(
        second.transform.TransformPoint({}),
        { 14.0F, 20.0F });
}

TEST(Drawing2DRenderPassTests, RejectsInvalidDescriptor)
{
    const auto pass = TGE::RenderPass2D::Create({});
    ASSERT_FALSE(pass);
    EXPECT_EQ(
        pass.error().code,
        TGE::RenderPass2DErrorCode::InvalidDescriptor);
}

TEST(Drawing2DRenderPassTests, ValidationRejectsInvalidImmediateCommands)
{
    auto passResult = TGE::RenderPass2D::Create({
        .target = TGE::RenderTarget::FromValues(8, 2)
    });
    ASSERT_TRUE(passResult);
    auto pass = std::move(*passResult);
    pass.Draw(TGE::Rectangle2D {});

    const auto validation = pass.Validate();
    ASSERT_FALSE(validation);
    EXPECT_EQ(
        validation.error().code,
        TGE::RenderingErrorCode::InvalidData);
}

TEST(Drawing2DRenderPassTests, ValidationRejectsCrossDeviceResources)
{
    auto passResult = TGE::RenderPass2D::Create({
        .target = TGE::RenderTarget::FromValues(8, 2)
    });
    ASSERT_TRUE(passResult);
    auto pass = std::move(*passResult);
    pass.Draw(TGE::Sprite2D {
        .region = {
            .texture = TGE::TextureView2D::FromValues(9, 1),
            .texelBounds = { .extent = { 16, 16 } }
        },
        .size = { 16.0F, 16.0F }
    });

    const auto validation = pass.Validate();
    ASSERT_FALSE(validation);
    EXPECT_EQ(
        validation.error().code,
        TGE::RenderingErrorCode::IncompatibleResource);
}
