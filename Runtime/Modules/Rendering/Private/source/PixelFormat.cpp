#include "TGE/Rendering/PixelFormat.hpp"

namespace TGE
{
    std::size_t BytesPerPixel(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::R8Unorm:
            return 1;
        case PixelFormat::Rg8Unorm:
        case PixelFormat::Depth16Unorm:
            return 2;
        case PixelFormat::Rgba8Unorm:
        case PixelFormat::Rgba8Srgb:
        case PixelFormat::Bgra8Unorm:
        case PixelFormat::Bgra8Srgb:
        case PixelFormat::Depth24Stencil8:
        case PixelFormat::Depth32Float:
            return 4;
        case PixelFormat::Rgba16Float:
            return 8;
        case PixelFormat::Rgba32Float:
            return 16;
        case PixelFormat::Undefined:
            return 0;
        }

        return 0;
    }

    bool IsSrgbFormat(PixelFormat format) noexcept
    {
        return format == PixelFormat::Rgba8Srgb ||
            format == PixelFormat::Bgra8Srgb;
    }

    bool IsColorFormat(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::R8Unorm:
        case PixelFormat::Rg8Unorm:
        case PixelFormat::Rgba8Unorm:
        case PixelFormat::Rgba8Srgb:
        case PixelFormat::Bgra8Unorm:
        case PixelFormat::Bgra8Srgb:
        case PixelFormat::Rgba16Float:
        case PixelFormat::Rgba32Float:
            return true;
        case PixelFormat::Undefined:
        case PixelFormat::Depth16Unorm:
        case PixelFormat::Depth24Stencil8:
        case PixelFormat::Depth32Float:
            return false;
        }

        return false;
    }

    bool IsDepthFormat(PixelFormat format) noexcept
    {
        return format == PixelFormat::Depth16Unorm ||
            format == PixelFormat::Depth24Stencil8 ||
            format == PixelFormat::Depth32Float;
    }

    bool HasStencil(PixelFormat format) noexcept
    {
        return format == PixelFormat::Depth24Stencil8;
    }
}
