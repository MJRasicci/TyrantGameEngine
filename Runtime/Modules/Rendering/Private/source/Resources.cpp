#include "TGE/Rendering/Resources.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>
#include <utility>

namespace
{
    TGE::RenderingError MakeError(
        TGE::RenderingErrorCode code,
        std::string message)
    {
        return { code, std::move(message) };
    }

    TGE::RenderingResult<void> ValidateSampleCount(
        TGE::SampleCount sampleCount)
    {
        switch (sampleCount)
        {
        case TGE::SampleCount::One:
        case TGE::SampleCount::Two:
        case TGE::SampleCount::Four:
        case TGE::SampleCount::Eight:
            return {};
        }

        return std::unexpected(MakeError(
            TGE::RenderingErrorCode::InvalidDescriptor,
            "The sample count is not a supported portable value."));
    }

    std::uint32_t MaximumMipLevelCount(TGE::Extent2U extent) noexcept
    {
        auto dimension = std::max(extent.width, extent.height);
        std::uint32_t levels = 0;
        while (dimension != 0)
        {
            ++levels;
            dimension >>= 1U;
        }
        return levels;
    }

    bool HasOnlyKnownTextureUsages(TGE::TextureUsage usage) noexcept
    {
        constexpr auto known =
            static_cast<std::uint32_t>(TGE::TextureUsage::Sampled) |
            static_cast<std::uint32_t>(TGE::TextureUsage::ColorAttachment) |
            static_cast<std::uint32_t>(
                TGE::TextureUsage::DepthStencilAttachment) |
            static_cast<std::uint32_t>(TGE::TextureUsage::TransferSource) |
            static_cast<std::uint32_t>(
                TGE::TextureUsage::TransferDestination) |
            static_cast<std::uint32_t>(TGE::TextureUsage::Storage);
        const auto value = static_cast<std::uint32_t>(usage);
        return (value & ~known) == 0;
    }

    template<class TRange>
    TGE::RenderingResult<void> ValidateUniqueNames(
        const TRange& bindings,
        std::string_view kind)
    {
        std::set<std::string_view, std::less<>> names;
        for (const auto& binding : bindings)
        {
            if (binding.name.empty())
            {
                return std::unexpected(MakeError(
                    TGE::RenderingErrorCode::InvalidDescriptor,
                    std::string(kind) + " binding names cannot be empty."));
            }
            if (!names.emplace(binding.name).second)
            {
                return std::unexpected(MakeError(
                    TGE::RenderingErrorCode::InvalidDescriptor,
                    std::string(kind) + " binding names must be unique."));
            }
        }
        return {};
    }
}

namespace TGE
{
    RenderingResult<void> Validate(
        const Texture2DDescriptor& descriptor)
    {
        if (descriptor.extent.IsEmpty())
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidExtent,
                "A texture extent must have non-zero width and height."));
        }
        if (BytesPerPixel(descriptor.format) == 0)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::UnsupportedFormat,
                "A texture must declare a concrete pixel format."));
        }
        if (descriptor.mipLevelCount == 0 ||
            descriptor.mipLevelCount >
                MaximumMipLevelCount(descriptor.extent))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "The texture mip-level count is invalid for its extent."));
        }
        if (descriptor.arrayLayerCount == 0)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A texture must contain at least one array layer."));
        }
        if (descriptor.usage == TextureUsage::None ||
            !HasOnlyKnownTextureUsages(descriptor.usage))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A texture must declare only known, non-empty usages."));
        }
        if (auto sampleResult = ValidateSampleCount(descriptor.sampleCount);
            !sampleResult)
        {
            return sampleResult;
        }
        if (descriptor.sampleCount != SampleCount::One &&
            descriptor.mipLevelCount != 1)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A multisampled texture cannot contain mip levels."));
        }
        if (HasTextureUsage(
                descriptor.usage,
                TextureUsage::ColorAttachment) &&
            !IsColorFormat(descriptor.format))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::IncompatibleResource,
                "A color attachment requires a color pixel format."));
        }
        if (HasTextureUsage(
                descriptor.usage,
                TextureUsage::DepthStencilAttachment) &&
            !IsDepthFormat(descriptor.format))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::IncompatibleResource,
                "A depth/stencil attachment requires a depth format."));
        }
        if (HasTextureUsage(
                descriptor.usage,
                TextureUsage::ColorAttachment) &&
            HasTextureUsage(
                descriptor.usage,
                TextureUsage::DepthStencilAttachment))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::IncompatibleResource,
                "One texture cannot be both a color and depth attachment."));
        }

        return {};
    }

    RenderingResult<void> Validate(
        const TextureView2DDescriptor& descriptor)
    {
        if (!descriptor.texture)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidResource,
                "A texture view requires a valid texture handle."));
        }
        if (descriptor.format &&
            BytesPerPixel(*descriptor.format) == 0)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::UnsupportedFormat,
                "A texture-view format override cannot be undefined."));
        }
        if (descriptor.mipLevelCount == 0 ||
            descriptor.arrayLayerCount == 0)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A texture view must select at least one mip and layer."));
        }

        switch (descriptor.aspect)
        {
        case TextureAspect::Color:
        case TextureAspect::Depth:
        case TextureAspect::Stencil:
        case TextureAspect::DepthStencil:
            break;
        default:
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "The texture-view aspect is invalid."));
        }

        if (descriptor.format)
        {
            const auto format = *descriptor.format;
            if (descriptor.aspect == TextureAspect::Color &&
                !IsColorFormat(format))
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::IncompatibleResource,
                    "A color texture view requires a color format."));
            }
            if ((descriptor.aspect == TextureAspect::Depth ||
                    descriptor.aspect == TextureAspect::DepthStencil) &&
                !IsDepthFormat(format))
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::IncompatibleResource,
                    "A depth texture view requires a depth format."));
            }
            if ((descriptor.aspect == TextureAspect::Stencil ||
                    descriptor.aspect == TextureAspect::DepthStencil) &&
                !HasStencil(format))
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::IncompatibleResource,
                    "A stencil texture view requires a stencil format."));
            }
        }

        return {};
    }

    RenderingResult<void> Validate(const SamplerDescriptor& descriptor)
    {
        const auto validFilter = [](FilterMode value)
        {
            switch (value)
            {
            case FilterMode::Nearest:
            case FilterMode::Linear:
                return true;
            }
            return false;
        };
        const auto validMipmapFilter = [](MipmapFilterMode value)
        {
            switch (value)
            {
            case MipmapFilterMode::Nearest:
            case MipmapFilterMode::Linear:
                return true;
            }
            return false;
        };
        const auto validAddress = [](AddressMode value)
        {
            switch (value)
            {
            case AddressMode::ClampToEdge:
            case AddressMode::Repeat:
            case AddressMode::MirroredRepeat:
                return true;
            }
            return false;
        };
        if (!validFilter(descriptor.minificationFilter) ||
            !validFilter(descriptor.magnificationFilter) ||
            !validMipmapFilter(descriptor.mipmapFilter) ||
            !validAddress(descriptor.addressU) ||
            !validAddress(descriptor.addressV))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "Sampler modes must use supported portable values."));
        }
        if (!std::isfinite(descriptor.minimumLod) ||
            !std::isfinite(descriptor.maximumLod) ||
            descriptor.minimumLod > descriptor.maximumLod)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "Sampler LOD bounds must be finite and ordered."));
        }
        if (descriptor.maximumAnisotropy &&
            (!std::isfinite(*descriptor.maximumAnisotropy) ||
                *descriptor.maximumAnisotropy < 1.0F))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "Sampler anisotropy must be finite and at least one."));
        }

        return {};
    }

    RenderingResult<void> Validate(
        const ShaderProgramDescriptor& descriptor)
    {
        if (descriptor.stages.empty())
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A shader program must contain at least one stage."));
        }

        bool hasVertex = false;
        bool hasFragment = false;
        bool hasCompute = false;
        for (const auto& stage : descriptor.stages)
        {
            if (stage.format != ShaderBinaryFormat::SpirV)
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::Unsupported,
                    "The shader binary format is unsupported."));
            }
            if (stage.bytecode.empty() ||
                stage.bytecode.size() % sizeof(std::uint32_t) != 0)
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::InvalidData,
                    "Shader bytecode must contain complete non-empty words."));
            }
            if (stage.entryPoint.empty())
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::InvalidDescriptor,
                    "A shader entry point cannot be empty."));
            }

            bool* present = nullptr;
            switch (stage.stage)
            {
            case ShaderStage::Vertex:
                present = &hasVertex;
                break;
            case ShaderStage::Fragment:
                present = &hasFragment;
                break;
            case ShaderStage::Compute:
                present = &hasCompute;
                break;
            default:
                return std::unexpected(MakeError(
                    RenderingErrorCode::InvalidDescriptor,
                    "The shader stage is invalid."));
            }
            if (*present)
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::InvalidDescriptor,
                    "A shader program cannot repeat a stage."));
            }
            *present = true;
        }

        if (hasCompute && (hasVertex || hasFragment))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A compute stage cannot be mixed with graphics stages."));
        }
        if (!hasCompute && (!hasVertex || !hasFragment))
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A graphics shader program requires vertex and fragment stages."));
        }

        return {};
    }

    RenderingResult<void> Validate(const Material2DDescriptor& descriptor)
    {
        if (!descriptor.shader)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidResource,
                "A material requires a valid shader-program handle."));
        }
        if (!descriptor.blendState.IsValid())
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidDescriptor,
                "A material requires a valid blend state."));
        }
        return {};
    }

    RenderingResult<void> Validate(
        const MaterialInstance2DDescriptor& descriptor)
    {
        if (!descriptor.material)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidResource,
                "A material instance requires a valid material handle."));
        }
        if (auto result = ValidateUniqueNames(
                descriptor.parameters,
                "Material parameter");
            !result)
        {
            return result;
        }
        if (auto result = ValidateUniqueNames(
                descriptor.textures,
                "Material texture");
            !result)
        {
            return result;
        }

        for (const auto& texture : descriptor.textures)
        {
            if (!texture.texture || !texture.sampler)
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::InvalidResource,
                    "Material texture bindings require valid texture-view and sampler handles."));
            }
            if (texture.texture.DomainValue() !=
                    descriptor.material.DomainValue() ||
                texture.sampler.DomainValue() !=
                    descriptor.material.DomainValue())
            {
                return std::unexpected(MakeError(
                    RenderingErrorCode::IncompatibleResource,
                    "Material resources must belong to one rendering device."));
            }
        }
        return {};
    }

    RenderingResult<void> Validate(
        const RenderTargetDescriptor& descriptor)
    {
        if (descriptor.extent.IsEmpty())
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::InvalidExtent,
                "A render-target extent must have non-zero width and height."));
        }
        if (!IsColorFormat(descriptor.colorFormat) ||
            BytesPerPixel(descriptor.colorFormat) == 0)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::UnsupportedFormat,
                "A render target requires a color pixel format."));
        }
        return ValidateSampleCount(descriptor.sampleCount);
    }

    RenderingResult<void> Validate(
        const RenderTexture2DDescriptor& descriptor)
    {
        if (descriptor.sampleCount != SampleCount::One)
        {
            return std::unexpected(MakeError(
                RenderingErrorCode::Unsupported,
                "RenderTexture2D does not implicitly resolve multisampling."));
        }

        Texture2DDescriptor texture {
            .extent = descriptor.extent,
            .format = descriptor.format,
            .mipLevelCount = descriptor.mipLevelCount,
            .sampleCount = descriptor.sampleCount,
            .usage = TextureUsage::Sampled |
                TextureUsage::ColorAttachment |
                descriptor.additionalUsage,
            .label = descriptor.label
        };
        return Validate(texture);
    }
}
