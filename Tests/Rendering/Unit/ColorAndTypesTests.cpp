#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "TGE/Rendering.hpp"

namespace
{
    TEST(RenderingColor, DecodesStandardSrgbTransferFunction)
    {
        const TGE::Srgba8 encoded {
            .red = 0,
            .green = 128,
            .blue = 255,
            .alpha = 64
        };

        const auto linear = encoded.ToLinear();

        EXPECT_FLOAT_EQ(linear.red, 0.0F);
        EXPECT_NEAR(linear.green, 0.2158605F, 0.000001F);
        EXPECT_FLOAT_EQ(linear.blue, 1.0F);
        EXPECT_NEAR(linear.alpha, 64.0F / 255.0F, 0.000001F);
        EXPECT_EQ(TGE::LinearColor::FromSrgba8(encoded), linear);
    }

    TEST(RenderingColor, EveryEightBitChannelRoundTripsThroughLinear)
    {
        for (std::uint32_t value = 0; value <= 255; ++value)
        {
            const auto channel = static_cast<std::uint8_t>(value);
            const TGE::Srgba8 encoded {
                .red = channel,
                .green = channel,
                .blue = channel,
                .alpha = channel
            };

            EXPECT_EQ(encoded.ToLinear().ToSrgba8(), encoded)
                << "channel value " << value;
        }
    }

    TEST(RenderingColor, EncodingClampsAndRoundsChannels)
    {
        const TGE::LinearColor linear {
            .red = -0.25F,
            .green = 0.0031308F,
            .blue = 1.25F,
            .alpha = 0.5F
        };

        EXPECT_EQ(
            linear.ToSrgba8(),
            (TGE::Srgba8 {
                .red = 0,
                .green = 10,
                .blue = 255,
                .alpha = 128
            }));
    }

    TEST(RenderingColor, PremultiplicationOnlyScalesColorChannels)
    {
        const TGE::LinearColor color {
            .red = 0.8F,
            .green = 0.4F,
            .blue = 0.2F,
            .alpha = 0.25F
        };

        EXPECT_EQ(
            color.Premultiplied(),
            (TGE::LinearColor {
                .red = 0.2F,
                .green = 0.1F,
                .blue = 0.05F,
                .alpha = 0.25F
            }));
        EXPECT_EQ(
            TGE::LinearColor::Transparent().Premultiplied(),
            TGE::LinearColor::Transparent());
    }

    TEST(RenderingTypes, ExtentIsEmptyWhenEitherDimensionIsZero)
    {
        EXPECT_TRUE(TGE::Extent2U {}.IsEmpty());
        EXPECT_TRUE((TGE::Extent2U { 0, 5 }).IsEmpty());
        EXPECT_TRUE((TGE::Extent2U { 7, 0 }).IsEmpty());
        EXPECT_FALSE((TGE::Extent2U { 7, 5 }).IsEmpty());
    }

    TEST(RenderingTypes, PixelRectUsesHalfOpenBounds)
    {
        const TGE::PixelRect rectangle {
            .position = { -3, 4 },
            .extent = { 5, 2 }
        };

        EXPECT_TRUE(rectangle.Contains({ -3, 4 }));
        EXPECT_TRUE(rectangle.Contains({ 1, 5 }));
        EXPECT_FALSE(rectangle.Contains({ -4, 4 }));
        EXPECT_FALSE(rectangle.Contains({ 2, 5 }));
        EXPECT_FALSE(rectangle.Contains({ 1, 6 }));
    }

    TEST(RenderingTypes, EmptyPixelRectContainsNoPoints)
    {
        const TGE::PixelRect zeroWidth {
            .position = { 10, 20 },
            .extent = { 0, 5 }
        };
        const TGE::PixelRect zeroHeight {
            .position = { 10, 20 },
            .extent = { 5, 0 }
        };

        EXPECT_TRUE(zeroWidth.IsEmpty());
        EXPECT_TRUE(zeroHeight.IsEmpty());
        EXPECT_FALSE(zeroWidth.Contains({ 10, 20 }));
        EXPECT_FALSE(zeroHeight.Contains({ 10, 20 }));
    }

    TEST(RenderingTypes, PixelRectContainmentDoesNotOverflowAtIntegerLimits)
    {
        constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
        constexpr auto minimum = std::numeric_limits<std::int32_t>::min();

        const TGE::PixelRect nearMaximum {
            .position = { maximum - 1, maximum - 1 },
            .extent = { std::numeric_limits<std::uint32_t>::max(), 2 }
        };
        const TGE::PixelRect nearMinimum {
            .position = { minimum, minimum },
            .extent = { 2, 2 }
        };

        EXPECT_TRUE(nearMaximum.Contains({ maximum, maximum }));
        EXPECT_TRUE(nearMinimum.Contains({ minimum, minimum }));
        EXPECT_TRUE(nearMinimum.Contains({ minimum + 1, minimum + 1 }));
    }
}
