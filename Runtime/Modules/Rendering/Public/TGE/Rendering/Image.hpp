#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "TGE/Export.hpp"
#include "TGE/Rendering/Color.hpp"
#include "TGE/Rendering/PixelFormat.hpp"
#include "TGE/Rendering/RenderingError.hpp"
#include "TGE/Rendering/Types.hpp"

namespace TGE
{
    /**
     * @brief Non-owning read-only view over decoded, uncompressed pixel data.
     *
     * The owner of pixels must outlive this view and every span obtained from
     * it. RowPitch may include padding after each logical row.
     */
    class TGE_API ImageView final
    {
    public:
        ImageView() noexcept = default;

        [[nodiscard]] static RenderingResult<ImageView> Create(
            Extent2U extent,
            PixelFormat format,
            std::span<const std::byte> pixels,
            std::size_t rowPitch = 0);

        [[nodiscard]] Extent2U Extent() const noexcept;
        [[nodiscard]] PixelFormat Format() const noexcept;
        [[nodiscard]] std::size_t RowPitch() const noexcept;
        [[nodiscard]] std::span<const std::byte> Pixels() const noexcept;
        [[nodiscard]] bool IsEmpty() const noexcept;

        [[nodiscard]] RenderingResult<std::span<const std::byte>> Row(
            std::uint32_t y) const;
        [[nodiscard]] RenderingResult<std::span<const std::byte>> Pixel(
            PixelPoint point) const;
        [[nodiscard]] RenderingResult<Srgba8> ReadSrgba8(
            PixelPoint point) const;

    private:
        friend class Image;

        ImageView(
            Extent2U extent,
            PixelFormat format,
            std::span<const std::byte> pixels,
            std::size_t rowPitch) noexcept;

        Extent2U extent {};
        PixelFormat format { PixelFormat::Undefined };
        std::span<const std::byte> pixels;
        std::size_t rowPitch {};
    };

    /**
     * @brief Owning CPU image with validated extent, format, and row layout.
     */
    class TGE_API Image final
    {
    public:
        Image() noexcept = default;

        [[nodiscard]] static RenderingResult<Image> Create(
            Extent2U extent,
            PixelFormat format,
            std::size_t rowPitch = 0);

        [[nodiscard]] static RenderingResult<Image> Copy(
            Extent2U extent,
            PixelFormat format,
            std::span<const std::byte> pixels,
            std::size_t rowPitch = 0);

        [[nodiscard]] Extent2U Extent() const noexcept;
        [[nodiscard]] PixelFormat Format() const noexcept;
        [[nodiscard]] std::size_t RowPitch() const noexcept;
        [[nodiscard]] std::span<const std::byte> Pixels() const noexcept;
        [[nodiscard]] std::span<std::byte> MutablePixels() noexcept;
        [[nodiscard]] bool IsEmpty() const noexcept;
        [[nodiscard]] ImageView View() const noexcept;

        [[nodiscard]] RenderingResult<std::span<const std::byte>> Row(
            std::uint32_t y) const;
        [[nodiscard]] RenderingResult<std::span<std::byte>> MutableRow(
            std::uint32_t y);
        [[nodiscard]] RenderingResult<std::span<const std::byte>> Pixel(
            PixelPoint point) const;
        [[nodiscard]] RenderingResult<std::span<std::byte>> MutablePixel(
            PixelPoint point);
        [[nodiscard]] RenderingResult<Srgba8> ReadSrgba8(
            PixelPoint point) const;
        [[nodiscard]] RenderingResult<void> WriteSrgba8(
            PixelPoint point,
            Srgba8 color);
        [[nodiscard]] RenderingResult<void> Fill(Srgba8 color);

    private:
        Image(
            Extent2U extent,
            PixelFormat format,
            std::size_t rowPitch,
            std::vector<std::byte> pixels) noexcept;

        Extent2U extent {};
        PixelFormat format { PixelFormat::Undefined };
        std::size_t rowPitch {};
        std::vector<std::byte> pixels;
    };
}
