#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "TGE/Rendering.hpp"

namespace
{
    TEST(RenderingImage, DefaultImageAndViewAreEmpty)
    {
        const TGE::Image image;
        const TGE::ImageView view;

        EXPECT_TRUE(image.IsEmpty());
        EXPECT_TRUE(view.IsEmpty());
        EXPECT_EQ(image.Format(), TGE::PixelFormat::Undefined);
        EXPECT_EQ(view.Format(), TGE::PixelFormat::Undefined);
        EXPECT_TRUE(image.Pixels().empty());
        EXPECT_TRUE(view.Pixels().empty());
    }

    TEST(RenderingImage, CreateBuildsTightlyPackedZeroInitializedStorage)
    {
        auto result = TGE::Image::Create(
            { 2, 3 },
            TGE::PixelFormat::Rgba8Srgb);

        ASSERT_TRUE(result) << result.error().message;
        const auto& image = *result;
        EXPECT_EQ(image.Extent(), (TGE::Extent2U { 2, 3 }));
        EXPECT_EQ(image.Format(), TGE::PixelFormat::Rgba8Srgb);
        EXPECT_EQ(image.RowPitch(), 8U);
        EXPECT_EQ(image.Pixels().size(), 24U);
        EXPECT_TRUE(std::ranges::all_of(
            image.Pixels(),
            [](std::byte value)
            {
                return value == std::byte {};
            }));

        auto row = image.Row(2);
        ASSERT_TRUE(row);
        EXPECT_EQ(row->size(), 8U);

        auto pixel = image.Pixel({ 1, 2 });
        ASSERT_TRUE(pixel);
        EXPECT_EQ(pixel->size(), 4U);
    }

    TEST(RenderingImage, ViewObservesWritesWithoutTakingOwnership)
    {
        auto result = TGE::Image::Create(
            { 2, 1 },
            TGE::PixelFormat::Rgba8Srgb);
        ASSERT_TRUE(result);
        auto image = std::move(*result);
        const auto view = image.View();

        ASSERT_TRUE(image.WriteSrgba8(
            { 1, 0 },
            { 11, 22, 33, 44 }));

        auto observed = view.ReadSrgba8({ 1, 0 });
        ASSERT_TRUE(observed);
        EXPECT_EQ(*observed, (TGE::Srgba8 { 11, 22, 33, 44 }));
        EXPECT_EQ(view.Extent(), image.Extent());
        EXPECT_EQ(view.RowPitch(), image.RowPitch());
    }

    TEST(RenderingImage, CopyOwnsBytesIndependentlyFromItsSource)
    {
        std::vector<std::byte> source {
            std::byte { 1 },
            std::byte { 2 },
            std::byte { 3 },
            std::byte { 4 },
            std::byte { 5 },
            std::byte { 6 },
            std::byte { 7 },
            std::byte { 8 }
        };

        auto result = TGE::Image::Copy(
            { 2, 1 },
            TGE::PixelFormat::Rgba8Srgb,
            source);
        ASSERT_TRUE(result) << result.error().message;
        auto image = std::move(*result);

        source[0] = std::byte { 99 };

        ASSERT_EQ(image.Pixels().size(), 8U);
        EXPECT_EQ(image.Pixels().front(), std::byte { 1 });
    }

    TEST(RenderingImage, PaddedRowsKeepPixelsAtTheDeclaredStride)
    {
        auto result = TGE::Image::Create(
            { 1, 2 },
            TGE::PixelFormat::Rgba8Srgb,
            8);
        ASSERT_TRUE(result) << result.error().message;
        auto image = std::move(*result);

        ASSERT_TRUE(image.WriteSrgba8({ 0, 0 }, { 1, 2, 3, 4 }));
        ASSERT_TRUE(image.WriteSrgba8({ 0, 1 }, { 5, 6, 7, 8 }));

        EXPECT_EQ(image.RowPitch(), 8U);
        ASSERT_GE(image.Pixels().size(), 12U);
        EXPECT_EQ(image.Pixels()[0], std::byte { 1 });
        EXPECT_EQ(image.Pixels()[3], std::byte { 4 });
        EXPECT_EQ(image.Pixels()[8], std::byte { 5 });
        EXPECT_EQ(image.Pixels()[11], std::byte { 8 });
    }

    TEST(RenderingImage, BgraStorageRoundTripsSrgbaChannelOrder)
    {
        auto result = TGE::Image::Create(
            { 1, 1 },
            TGE::PixelFormat::Bgra8Srgb);
        ASSERT_TRUE(result);
        auto image = std::move(*result);

        ASSERT_TRUE(image.WriteSrgba8(
            { 0, 0 },
            { 10, 20, 30, 40 }));
        ASSERT_EQ(image.Pixels().size(), 4U);
        EXPECT_EQ(image.Pixels()[0], std::byte { 30 });
        EXPECT_EQ(image.Pixels()[1], std::byte { 20 });
        EXPECT_EQ(image.Pixels()[2], std::byte { 10 });
        EXPECT_EQ(image.Pixels()[3], std::byte { 40 });

        auto color = image.ReadSrgba8({ 0, 0 });
        ASSERT_TRUE(color);
        EXPECT_EQ(*color, (TGE::Srgba8 { 10, 20, 30, 40 }));
    }

    TEST(RenderingImage, LinearUnormStorageConvertsToAndFromSrgb)
    {
        auto result = TGE::Image::Create(
            { 1, 1 },
            TGE::PixelFormat::Rgba8Unorm);
        ASSERT_TRUE(result);
        auto image = std::move(*result);

        ASSERT_TRUE(image.WriteSrgba8(
            { 0, 0 },
            { 128, 128, 128, 128 }));
        ASSERT_EQ(image.Pixels().size(), 4U);
        EXPECT_EQ(image.Pixels()[0], std::byte { 55 });
        EXPECT_EQ(image.Pixels()[1], std::byte { 55 });
        EXPECT_EQ(image.Pixels()[2], std::byte { 55 });
        EXPECT_EQ(image.Pixels()[3], std::byte { 128 });

        auto color = image.ReadSrgba8({ 0, 0 });
        ASSERT_TRUE(color);
        EXPECT_EQ(*color, (TGE::Srgba8 { 128, 128, 128, 128 }));
    }

    TEST(RenderingImage, FillWritesEveryLogicalPixel)
    {
        auto result = TGE::Image::Create(
            { 3, 2 },
            TGE::PixelFormat::Rgba8Srgb);
        ASSERT_TRUE(result);
        auto image = std::move(*result);
        const TGE::Srgba8 expected { 17, 34, 51, 68 };

        ASSERT_TRUE(image.Fill(expected));

        for (std::int32_t y = 0; y < 2; ++y)
        {
            for (std::int32_t x = 0; x < 3; ++x)
            {
                auto color = image.ReadSrgba8({ x, y });
                ASSERT_TRUE(color);
                EXPECT_EQ(*color, expected);
            }
        }
    }

    TEST(RenderingImage, FactoriesRejectInvalidLayoutsBeforeAccess)
    {
        const std::vector<std::byte> tooShort(15);

        auto zeroExtent = TGE::Image::Create(
            { 0, 2 },
            TGE::PixelFormat::Rgba8Srgb);
        ASSERT_FALSE(zeroExtent);
        EXPECT_EQ(
            zeroExtent.error().code,
            TGE::RenderingErrorCode::InvalidExtent);

        auto undefinedFormat = TGE::Image::Create(
            { 1, 1 },
            TGE::PixelFormat::Undefined);
        ASSERT_FALSE(undefinedFormat);
        EXPECT_EQ(
            undefinedFormat.error().code,
            TGE::RenderingErrorCode::UnsupportedFormat);

        auto narrowPitch = TGE::Image::Create(
            { 2, 2 },
            TGE::PixelFormat::Rgba8Srgb,
            7);
        ASSERT_FALSE(narrowPitch);
        EXPECT_EQ(
            narrowPitch.error().code,
            TGE::RenderingErrorCode::InvalidData);

        auto shortCopy = TGE::Image::Copy(
            { 2, 2 },
            TGE::PixelFormat::Rgba8Srgb,
            tooShort);
        ASSERT_FALSE(shortCopy);
        EXPECT_EQ(
            shortCopy.error().code,
            TGE::RenderingErrorCode::InvalidData);
    }

    TEST(RenderingImage, FactoryDetectsStorageSizeOverflow)
    {
        auto result = TGE::Image::Create(
            {
                std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max()
            },
            TGE::PixelFormat::Rgba32Float);

        ASSERT_FALSE(result);
        EXPECT_EQ(
            result.error().code,
            TGE::RenderingErrorCode::ArithmeticOverflow);
    }

    TEST(RenderingImage, RowAndPixelAccessReportOutOfBounds)
    {
        auto result = TGE::Image::Create(
            { 2, 2 },
            TGE::PixelFormat::Rgba8Srgb);
        ASSERT_TRUE(result);
        auto image = std::move(*result);

        auto row = image.Row(2);
        ASSERT_FALSE(row);
        EXPECT_EQ(row.error().code, TGE::RenderingErrorCode::OutOfBounds);

        for (const auto point : {
                 TGE::PixelPoint { -1, 0 },
                 TGE::PixelPoint { 0, -1 },
                 TGE::PixelPoint { 2, 0 },
                 TGE::PixelPoint { 0, 2 } })
        {
            auto pixel = image.Pixel(point);
            ASSERT_FALSE(pixel);
            EXPECT_EQ(
                pixel.error().code,
                TGE::RenderingErrorCode::OutOfBounds);
        }
    }

    TEST(RenderingImage, SrgbaOperationsRejectUnsupportedPixelFormats)
    {
        auto result = TGE::Image::Create(
            { 1, 1 },
            TGE::PixelFormat::R8Unorm);
        ASSERT_TRUE(result);
        auto image = std::move(*result);

        auto read = image.ReadSrgba8({ 0, 0 });
        auto write = image.WriteSrgba8({ 0, 0 }, TGE::Srgba8::White());
        auto fill = image.Fill(TGE::Srgba8::White());

        ASSERT_FALSE(read);
        ASSERT_FALSE(write);
        ASSERT_FALSE(fill);
        EXPECT_EQ(
            read.error().code,
            TGE::RenderingErrorCode::UnsupportedFormat);
        EXPECT_EQ(
            write.error().code,
            TGE::RenderingErrorCode::UnsupportedFormat);
        EXPECT_EQ(
            fill.error().code,
            TGE::RenderingErrorCode::UnsupportedFormat);
    }

    TEST(RenderingImage, ImageViewValidatesBorrowedStorage)
    {
        const std::vector<std::byte> pixels(16);

        auto valid = TGE::ImageView::Create(
            { 2, 2 },
            TGE::PixelFormat::Rgba8Srgb,
            pixels);
        ASSERT_TRUE(valid) << valid.error().message;
        EXPECT_FALSE(valid->IsEmpty());

        auto tooShort = TGE::ImageView::Create(
            { 2, 2 },
            TGE::PixelFormat::Rgba8Srgb,
            std::span<const std::byte>(pixels).first(15));
        ASSERT_FALSE(tooShort);
        EXPECT_EQ(
            tooShort.error().code,
            TGE::RenderingErrorCode::InvalidData);
    }
}
