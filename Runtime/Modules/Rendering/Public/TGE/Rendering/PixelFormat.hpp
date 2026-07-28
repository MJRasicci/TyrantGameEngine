#pragma once

#include <cstddef>
#include <cstdint>

#include "TGE/Export.hpp"

namespace TGE
{
    /**
     * @brief Backend-neutral texel and attachment formats.
     *
     * The nonlinear encoding is part of the format so a texture cannot carry
     * contradictory format and color-space metadata.
     */
    enum class PixelFormat
    {
        Undefined,
        R8Unorm,
        Rg8Unorm,
        Rgba8Unorm,
        Rgba8Srgb,
        Bgra8Unorm,
        Bgra8Srgb,
        Rgba16Float,
        Rgba32Float,
        Depth16Unorm,
        Depth24Stencil8,
        Depth32Float
    };

    [[nodiscard]] TGE_API std::size_t BytesPerPixel(
        PixelFormat format) noexcept;
    [[nodiscard]] TGE_API bool IsSrgbFormat(PixelFormat format) noexcept;
    [[nodiscard]] TGE_API bool IsColorFormat(PixelFormat format) noexcept;
    [[nodiscard]] TGE_API bool IsDepthFormat(PixelFormat format) noexcept;
    [[nodiscard]] TGE_API bool HasStencil(PixelFormat format) noexcept;

    enum class TextureUsage : std::uint32_t
    {
        None = 0,
        Sampled = 1U << 0U,
        ColorAttachment = 1U << 1U,
        DepthStencilAttachment = 1U << 2U,
        TransferSource = 1U << 3U,
        TransferDestination = 1U << 4U,
        Storage = 1U << 5U
    };

    [[nodiscard]] constexpr TextureUsage operator|(
        TextureUsage left,
        TextureUsage right) noexcept
    {
        return static_cast<TextureUsage>(
            static_cast<std::uint32_t>(left) |
            static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr TextureUsage operator&(
        TextureUsage left,
        TextureUsage right) noexcept
    {
        return static_cast<TextureUsage>(
            static_cast<std::uint32_t>(left) &
            static_cast<std::uint32_t>(right));
    }

    constexpr TextureUsage& operator|=(
        TextureUsage& left,
        TextureUsage right) noexcept
    {
        left = left | right;
        return left;
    }

    [[nodiscard]] constexpr bool HasTextureUsage(
        TextureUsage value,
        TextureUsage required) noexcept
    {
        return (value & required) == required;
    }

    enum class SampleCount : std::uint8_t
    {
        One = 1,
        Two = 2,
        Four = 4,
        Eight = 8
    };
}
