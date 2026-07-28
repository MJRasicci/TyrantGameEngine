#include "TGE/Rendering/Image.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
    using TGE::Extent2U;
    using TGE::PixelFormat;
    using TGE::RenderingError;
    using TGE::RenderingErrorCode;
    using TGE::RenderingResult;

    struct ImageLayout
    {
        std::size_t bytesPerPixel {};
        std::size_t tightRowBytes {};
        std::size_t rowPitch {};
        std::size_t requiredSourceBytes {};
        std::size_t allocationBytes {};
    };

    RenderingError MakeError(
        RenderingErrorCode code,
        std::string message)
    {
        return { code, std::move(message) };
    }

    RenderingResult<ImageLayout> ResolveLayout(
        Extent2U extent,
        PixelFormat format,
        std::size_t requestedRowPitch)
    {
        if (extent.IsEmpty())
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidExtent,
                "An image extent must have non-zero width and height."));
        }

        const auto bytesPerPixel = TGE::BytesPerPixel(format);
        if (bytesPerPixel == 0)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::UnsupportedFormat,
                "The image pixel format is undefined or unsupported."));
        }

        constexpr auto maximum = std::numeric_limits<std::size_t>::max();
        if (static_cast<std::size_t>(extent.width) >
            maximum / bytesPerPixel)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::ArithmeticOverflow,
                "The image row size exceeds the addressable size."));
        }

        const auto tightRowBytes =
            static_cast<std::size_t>(extent.width) * bytesPerPixel;
        const auto rowPitch = requestedRowPitch == 0
            ? tightRowBytes
            : requestedRowPitch;
        if (rowPitch < tightRowBytes)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidData,
                "The image row pitch is smaller than one logical row."));
        }

        const auto precedingRows =
            static_cast<std::size_t>(extent.height - 1U);
        if (precedingRows != 0 &&
            rowPitch > (maximum - tightRowBytes) / precedingRows)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::ArithmeticOverflow,
                "The image byte size exceeds the addressable size."));
        }
        const auto requiredSourceBytes =
            precedingRows * rowPitch + tightRowBytes;

        if (static_cast<std::size_t>(extent.height) >
            maximum / rowPitch)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::ArithmeticOverflow,
                "The image allocation size exceeds the addressable size."));
        }

        return ImageLayout {
            .bytesPerPixel = bytesPerPixel,
            .tightRowBytes = tightRowBytes,
            .rowPitch = rowPitch,
            .requiredSourceBytes = requiredSourceBytes,
            .allocationBytes =
                static_cast<std::size_t>(extent.height) * rowPitch
        };
    }

    RenderingResult<std::size_t> PixelOffset(
        Extent2U extent,
        std::size_t rowPitch,
        std::size_t bytesPerPixel,
        TGE::PixelPoint point)
    {
        if (point.x < 0 ||
            point.y < 0 ||
            static_cast<std::uint32_t>(point.x) >= extent.width ||
            static_cast<std::uint32_t>(point.y) >= extent.height)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::OutOfBounds,
                "The pixel coordinate is outside the image extent."));
        }

        return static_cast<std::size_t>(point.y) * rowPitch +
            static_cast<std::size_t>(point.x) * bytesPerPixel;
    }

    float Saturate(float value) noexcept
    {
        if (std::isnan(value))
        {
            return 0.0F;
        }
        return std::clamp(value, 0.0F, 1.0F);
    }

    std::uint8_t EncodeUnorm(float value) noexcept
    {
        return static_cast<std::uint8_t>(
            std::lround(Saturate(value) * 255.0F));
    }

    TGE::Srgba8 ReadFourChannelPixel(
        std::span<const std::byte> pixel,
        PixelFormat format)
    {
        auto channel = [&pixel](std::size_t index)
        {
            return std::to_integer<std::uint8_t>(pixel[index]);
        };

        if (format == PixelFormat::Rgba8Srgb)
        {
            return { channel(0), channel(1), channel(2), channel(3) };
        }
        if (format == PixelFormat::Bgra8Srgb)
        {
            return { channel(2), channel(1), channel(0), channel(3) };
        }

        const TGE::LinearColor linear = format == PixelFormat::Rgba8Unorm
            ? TGE::LinearColor {
                static_cast<float>(channel(0)) / 255.0F,
                static_cast<float>(channel(1)) / 255.0F,
                static_cast<float>(channel(2)) / 255.0F,
                static_cast<float>(channel(3)) / 255.0F
            }
            : TGE::LinearColor {
                static_cast<float>(channel(2)) / 255.0F,
                static_cast<float>(channel(1)) / 255.0F,
                static_cast<float>(channel(0)) / 255.0F,
                static_cast<float>(channel(3)) / 255.0F
            };
        return linear.ToSrgba8();
    }

    void WriteFourChannelPixel(
        std::span<std::byte> pixel,
        PixelFormat format,
        TGE::Srgba8 color)
    {
        auto writeRgba = [&pixel](
            std::uint8_t red,
            std::uint8_t green,
            std::uint8_t blue,
            std::uint8_t alpha,
            bool bgra)
        {
            pixel[0] = static_cast<std::byte>(bgra ? blue : red);
            pixel[1] = static_cast<std::byte>(green);
            pixel[2] = static_cast<std::byte>(bgra ? red : blue);
            pixel[3] = static_cast<std::byte>(alpha);
        };

        const bool bgra = format == PixelFormat::Bgra8Srgb ||
            format == PixelFormat::Bgra8Unorm;
        if (format == PixelFormat::Rgba8Srgb ||
            format == PixelFormat::Bgra8Srgb)
        {
            writeRgba(
                color.red,
                color.green,
                color.blue,
                color.alpha,
                bgra);
            return;
        }

        const auto linear = color.ToLinear();
        writeRgba(
            EncodeUnorm(linear.red),
            EncodeUnorm(linear.green),
            EncodeUnorm(linear.blue),
            EncodeUnorm(linear.alpha),
            bgra);
    }

    bool SupportsSrgbaAccess(PixelFormat format) noexcept
    {
        return format == PixelFormat::Rgba8Unorm ||
            format == PixelFormat::Rgba8Srgb ||
            format == PixelFormat::Bgra8Unorm ||
            format == PixelFormat::Bgra8Srgb;
    }
}

namespace TGE
{
    ImageView::ImageView(
        Extent2U extent,
        PixelFormat format,
        std::span<const std::byte> pixels,
        std::size_t rowPitch) noexcept
        : extent(extent),
          format(format),
          pixels(pixels),
          rowPitch(rowPitch)
    {
    }

    RenderingResult<ImageView> ImageView::Create(
        Extent2U extent,
        PixelFormat format,
        std::span<const std::byte> pixels,
        std::size_t rowPitch)
    {
        auto layout = ResolveLayout(extent, format, rowPitch);
        if (!layout)
        {
            return std::unexpected(std::move(layout.error()));
        }
        if (pixels.size() < layout->requiredSourceBytes)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidData,
                "The pixel span is smaller than the declared image layout."));
        }

        return ImageView(extent, format, pixels, layout->rowPitch);
    }

    Extent2U ImageView::Extent() const noexcept
    {
        return extent;
    }

    PixelFormat ImageView::Format() const noexcept
    {
        return format;
    }

    std::size_t ImageView::RowPitch() const noexcept
    {
        return rowPitch;
    }

    std::span<const std::byte> ImageView::Pixels() const noexcept
    {
        return pixels;
    }

    bool ImageView::IsEmpty() const noexcept
    {
        return extent.IsEmpty() || pixels.empty();
    }

    RenderingResult<std::span<const std::byte>> ImageView::Row(
        std::uint32_t y) const
    {
        if (y >= extent.height)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::OutOfBounds,
                "The row index is outside the image extent."));
        }

        const auto rowBytes =
            static_cast<std::size_t>(extent.width) * BytesPerPixel(format);
        return pixels.subspan(static_cast<std::size_t>(y) * rowPitch, rowBytes);
    }

    RenderingResult<std::span<const std::byte>> ImageView::Pixel(
        PixelPoint point) const
    {
        const auto bytesPerPixel = BytesPerPixel(format);
        auto offset = PixelOffset(
            extent,
            rowPitch,
            bytesPerPixel,
            point);
        if (!offset)
        {
            return std::unexpected(std::move(offset.error()));
        }
        return pixels.subspan(*offset, bytesPerPixel);
    }

    RenderingResult<Srgba8> ImageView::ReadSrgba8(
        PixelPoint point) const
    {
        if (!SupportsSrgbaAccess(format))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::UnsupportedFormat,
                "sRGBA access requires an RGBA8 or BGRA8 image format."));
        }

        auto pixel = Pixel(point);
        if (!pixel)
        {
            return std::unexpected(std::move(pixel.error()));
        }
        return ReadFourChannelPixel(*pixel, format);
    }

    Image::Image(
        Extent2U extent,
        PixelFormat format,
        std::size_t rowPitch,
        std::vector<std::byte> pixels) noexcept
        : extent(extent),
          format(format),
          rowPitch(rowPitch),
          pixels(std::move(pixels))
    {
    }

    RenderingResult<Image> Image::Create(
        Extent2U extent,
        PixelFormat format,
        std::size_t rowPitch)
    {
        auto layout = ResolveLayout(extent, format, rowPitch);
        if (!layout)
        {
            return std::unexpected(std::move(layout.error()));
        }

        try
        {
            return Image(
                extent,
                format,
                layout->rowPitch,
                std::vector<std::byte>(layout->allocationBytes));
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::OutOfMemory,
                "The CPU image allocation failed."));
        }
        catch (const std::length_error&)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::ArithmeticOverflow,
                "The CPU image allocation exceeds the container limit."));
        }
    }

    RenderingResult<Image> Image::Copy(
        Extent2U extent,
        PixelFormat format,
        std::span<const std::byte> pixels,
        std::size_t rowPitch)
    {
        auto source = ImageView::Create(
            extent,
            format,
            pixels,
            rowPitch);
        if (!source)
        {
            return std::unexpected(std::move(source.error()));
        }

        auto image = Create(extent, format, source->RowPitch());
        if (!image)
        {
            return std::unexpected(std::move(image.error()));
        }

        for (std::uint32_t y = 0; y < extent.height; ++y)
        {
            auto sourceRow = source->Row(y);
            auto destinationRow = image->MutableRow(y);
            std::ranges::copy(*sourceRow, destinationRow->begin());
        }
        return image;
    }

    Extent2U Image::Extent() const noexcept
    {
        return extent;
    }

    PixelFormat Image::Format() const noexcept
    {
        return format;
    }

    std::size_t Image::RowPitch() const noexcept
    {
        return rowPitch;
    }

    std::span<const std::byte> Image::Pixels() const noexcept
    {
        return pixels;
    }

    std::span<std::byte> Image::MutablePixels() noexcept
    {
        return pixels;
    }

    bool Image::IsEmpty() const noexcept
    {
        return extent.IsEmpty() || pixels.empty();
    }

    ImageView Image::View() const noexcept
    {
        if (IsEmpty())
        {
            return {};
        }
        return ImageView(extent, format, pixels, rowPitch);
    }

    RenderingResult<std::span<const std::byte>> Image::Row(
        std::uint32_t y) const
    {
        return View().Row(y);
    }

    RenderingResult<std::span<std::byte>> Image::MutableRow(
        std::uint32_t y)
    {
        if (y >= extent.height)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::OutOfBounds,
                "The row index is outside the image extent."));
        }

        const auto rowBytes =
            static_cast<std::size_t>(extent.width) * BytesPerPixel(format);
        return std::span<std::byte>(pixels).subspan(
            static_cast<std::size_t>(y) * rowPitch,
            rowBytes);
    }

    RenderingResult<std::span<const std::byte>> Image::Pixel(
        PixelPoint point) const
    {
        return View().Pixel(point);
    }

    RenderingResult<std::span<std::byte>> Image::MutablePixel(
        PixelPoint point)
    {
        const auto bytesPerPixel = BytesPerPixel(format);
        auto offset = PixelOffset(
            extent,
            rowPitch,
            bytesPerPixel,
            point);
        if (!offset)
        {
            return std::unexpected(std::move(offset.error()));
        }
        return std::span<std::byte>(pixels).subspan(*offset, bytesPerPixel);
    }

    RenderingResult<Srgba8> Image::ReadSrgba8(
        PixelPoint point) const
    {
        return View().ReadSrgba8(point);
    }

    RenderingResult<void> Image::WriteSrgba8(
        PixelPoint point,
        Srgba8 color)
    {
        if (!SupportsSrgbaAccess(format))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::UnsupportedFormat,
                "sRGBA access requires an RGBA8 or BGRA8 image format."));
        }

        auto pixel = MutablePixel(point);
        if (!pixel)
        {
            return std::unexpected(std::move(pixel.error()));
        }
        WriteFourChannelPixel(*pixel, format, color);
        return {};
    }

    RenderingResult<void> Image::Fill(Srgba8 color)
    {
        if (!SupportsSrgbaAccess(format))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::UnsupportedFormat,
                "sRGBA fill requires an RGBA8 or BGRA8 image format."));
        }

        for (std::uint32_t y = 0; y < extent.height; ++y)
        {
            for (std::uint32_t x = 0; x < extent.width; ++x)
            {
                const auto result = WriteSrgba8(
                    PixelPoint {
                        static_cast<std::int32_t>(x),
                        static_cast<std::int32_t>(y)
                    },
                    color);
                if (!result)
                {
                    return result;
                }
            }
        }
        return {};
    }
}
