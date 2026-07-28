/**
 * @file IRenderingDevice.hpp
 * @brief Backend-neutral creation and lifetime boundary for render resources.
 */

#pragma once

#include <cstdint>

#include "TGE/Export.hpp"
#include "TGE/Rendering/Image.hpp"
#include "TGE/Rendering/Resources.hpp"

namespace TGE
{
    struct TextureUpload2D
    {
        Texture2D texture;
        std::uint32_t mipLevel {};
        std::uint32_t arrayLayer {};
        PixelPoint destination {};
        ImageView source;
    };

    /**
     * @brief Device-owned registry for portable rendering resources.
     *
     * Every handle returned by one device uses DomainValue(). Public handle
     * constructors are intended for backend adapters and tests; IsAlive is the
     * authoritative validity check.
     *
     * Creation and upload consume borrowed image data synchronously. Destroy
     * invalidates a handle for future work, while implementations must defer
     * native destruction until already-submitted frames have completed.
     */
    class TGE_API IRenderingDevice
    {
    public:
        virtual ~IRenderingDevice() = default;

        [[nodiscard]] virtual std::uint64_t DomainValue() const noexcept = 0;

        [[nodiscard]] virtual RenderingResult<Texture2D> CreateTexture2D(
            const Texture2DDescriptor& descriptor,
            ImageView initialData = {}) = 0;
        [[nodiscard]] virtual RenderingResult<void> UploadTexture2D(
            const TextureUpload2D& upload) = 0;
        [[nodiscard]] virtual RenderingResult<TextureView2D>
            CreateTextureView2D(
                const TextureView2DDescriptor& descriptor) = 0;
        [[nodiscard]] virtual RenderingResult<Sampler> CreateSampler(
            const SamplerDescriptor& descriptor) = 0;
        [[nodiscard]] virtual RenderingResult<ShaderProgram>
            CreateShaderProgram(
                const ShaderProgramDescriptor& descriptor) = 0;
        [[nodiscard]] virtual RenderingResult<Material2D> CreateMaterial2D(
            const Material2DDescriptor& descriptor) = 0;
        [[nodiscard]] virtual RenderingResult<MaterialInstance2D>
            CreateMaterialInstance2D(
                const MaterialInstance2DDescriptor& descriptor) = 0;
        [[nodiscard]] virtual RenderingResult<RenderTexture2D>
            CreateRenderTexture2D(
                const RenderTexture2DDescriptor& descriptor) = 0;

        [[nodiscard]] virtual RenderingResult<RenderTargetDescriptor> Describe(
            RenderTarget target) const = 0;

        [[nodiscard]] virtual bool IsAlive(Texture2D resource)
            const noexcept = 0;
        [[nodiscard]] virtual bool IsAlive(TextureView2D resource)
            const noexcept = 0;
        [[nodiscard]] virtual bool IsAlive(Sampler resource)
            const noexcept = 0;
        [[nodiscard]] virtual bool IsAlive(ShaderProgram resource)
            const noexcept = 0;
        [[nodiscard]] virtual bool IsAlive(Material2D resource)
            const noexcept = 0;
        [[nodiscard]] virtual bool IsAlive(MaterialInstance2D resource)
            const noexcept = 0;
        [[nodiscard]] virtual bool IsAlive(RenderTarget resource)
            const noexcept = 0;

        [[nodiscard]] virtual RenderingResult<void> Destroy(
            Texture2D resource) = 0;
        [[nodiscard]] virtual RenderingResult<void> Destroy(
            TextureView2D resource) = 0;
        [[nodiscard]] virtual RenderingResult<void> Destroy(
            Sampler resource) = 0;
        [[nodiscard]] virtual RenderingResult<void> Destroy(
            ShaderProgram resource) = 0;
        [[nodiscard]] virtual RenderingResult<void> Destroy(
            Material2D resource) = 0;
        [[nodiscard]] virtual RenderingResult<void> Destroy(
            MaterialInstance2D resource) = 0;
        [[nodiscard]] virtual RenderingResult<void> Destroy(
            RenderTexture2D resource) = 0;
    };
}
