#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "TGE/Export.hpp"
#include "TGE/Rendering/BlendState.hpp"
#include "TGE/Rendering/Color.hpp"
#include "TGE/Rendering/PixelFormat.hpp"
#include "TGE/Rendering/RenderingError.hpp"
#include "TGE/Rendering/ResourceHandle.hpp"
#include "TGE/Rendering/Types.hpp"

namespace TGE
{
    struct Texture2DDescriptor
    {
        Extent2U extent {};
        PixelFormat format { PixelFormat::Undefined };
        std::uint32_t mipLevelCount { 1 };
        std::uint32_t arrayLayerCount { 1 };
        SampleCount sampleCount { SampleCount::One };
        TextureUsage usage { TextureUsage::Sampled |
            TextureUsage::TransferDestination };
        std::string label;

        bool operator==(const Texture2DDescriptor&) const = default;
    };

    enum class TextureAspect
    {
        Color,
        Depth,
        Stencil,
        DepthStencil
    };

    struct TextureView2DDescriptor
    {
        Texture2D texture;
        std::optional<PixelFormat> format;
        TextureAspect aspect { TextureAspect::Color };
        std::uint32_t baseMipLevel {};
        std::uint32_t mipLevelCount { 1 };
        std::uint32_t baseArrayLayer {};
        std::uint32_t arrayLayerCount { 1 };
        std::string label;

        bool operator==(const TextureView2DDescriptor&) const = default;
    };

    enum class FilterMode
    {
        Nearest,
        Linear
    };

    enum class MipmapFilterMode
    {
        Nearest,
        Linear
    };

    enum class AddressMode
    {
        ClampToEdge,
        Repeat,
        MirroredRepeat
    };

    struct SamplerDescriptor
    {
        FilterMode minificationFilter { FilterMode::Linear };
        FilterMode magnificationFilter { FilterMode::Linear };
        MipmapFilterMode mipmapFilter { MipmapFilterMode::Linear };
        AddressMode addressU { AddressMode::ClampToEdge };
        AddressMode addressV { AddressMode::ClampToEdge };
        float minimumLod {};
        float maximumLod { 32.0F };
        std::optional<float> maximumAnisotropy;
        std::string label;

        bool operator==(const SamplerDescriptor&) const = default;
    };

    enum class ShaderStage
    {
        Vertex,
        Fragment,
        Compute
    };

    enum class ShaderBinaryFormat
    {
        /**
         * Engine-canonical shader IR. Non-Vulkan backends are responsible for
         * translating or cross-compiling it during program creation.
         */
        SpirV
    };

    struct ShaderStageDescriptor
    {
        ShaderStage stage { ShaderStage::Vertex };
        ShaderBinaryFormat format { ShaderBinaryFormat::SpirV };
        std::vector<std::byte> bytecode;
        std::string entryPoint { "main" };

        bool operator==(const ShaderStageDescriptor&) const = default;
    };

    struct ShaderProgramDescriptor
    {
        std::vector<ShaderStageDescriptor> stages;
        std::string label;

        bool operator==(const ShaderProgramDescriptor&) const = default;
    };

    struct Material2DDescriptor
    {
        ShaderProgram shader;
        BlendState blendState { BlendState::PremultipliedAlpha() };
        std::string label;

        bool operator==(const Material2DDescriptor&) const = default;
    };

    using MaterialParameterValue2D = std::variant<
        float,
        std::int32_t,
        std::uint32_t,
        std::array<float, 2>,
        std::array<float, 4>,
        LinearColor>;

    struct MaterialParameter2D
    {
        std::string name;
        MaterialParameterValue2D value;

        bool operator==(const MaterialParameter2D&) const = default;
    };

    struct MaterialTextureBinding2D
    {
        std::string name;
        TextureView2D texture;
        Sampler sampler;

        bool operator==(const MaterialTextureBinding2D&) const = default;
    };

    struct MaterialInstance2DDescriptor
    {
        Material2D material;
        std::vector<MaterialParameter2D> parameters;
        std::vector<MaterialTextureBinding2D> textures;
        std::string label;

        bool operator==(const MaterialInstance2DDescriptor&) const = default;
    };

    struct RenderTargetDescriptor
    {
        Extent2U extent {};
        PixelFormat colorFormat { PixelFormat::Undefined };
        SampleCount sampleCount { SampleCount::One };
        bool presentable { false };
        std::string label;

        bool operator==(const RenderTargetDescriptor&) const = default;
    };

    struct RenderTexture2DDescriptor
    {
        Extent2U extent {};
        PixelFormat format { PixelFormat::Rgba8Srgb };
        std::uint32_t mipLevelCount { 1 };
        SampleCount sampleCount { SampleCount::One };
        TextureUsage additionalUsage { TextureUsage::None };
        std::string label;

        bool operator==(const RenderTexture2DDescriptor&) const = default;
    };

    /**
     * @brief Convenience bundle for an offscreen target and its sampleable
     * color resource.
     */
    struct RenderTexture2D
    {
        RenderTarget target;
        Texture2D texture;
        TextureView2D view;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return target &&
                   texture &&
                   view &&
                   target.DomainValue() == texture.DomainValue() &&
                   target.DomainValue() == view.DomainValue();
        }

        bool operator==(const RenderTexture2D&) const = default;
    };

    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const Texture2DDescriptor& descriptor);
    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const TextureView2DDescriptor& descriptor);
    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const SamplerDescriptor& descriptor);
    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const ShaderProgramDescriptor& descriptor);
    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const Material2DDescriptor& descriptor);
    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const MaterialInstance2DDescriptor& descriptor);
    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const RenderTargetDescriptor& descriptor);
    [[nodiscard]] TGE_API RenderingResult<void> Validate(
        const RenderTexture2DDescriptor& descriptor);
}
