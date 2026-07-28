#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "TGE/Rendering.hpp"

namespace
{
    static_assert(!std::is_same_v<TGE::Texture2D, TGE::TextureView2D>);
    static_assert(!std::is_convertible_v<TGE::Texture2D, TGE::TextureView2D>);
    static_assert(std::is_trivially_copyable_v<TGE::Texture2D>);

    TGE::ShaderStageDescriptor MakeShaderStage(TGE::ShaderStage stage)
    {
        return {
            .stage = stage,
            .format = TGE::ShaderBinaryFormat::SpirV,
            .bytecode = std::vector<std::byte>(4),
            .entryPoint = "main"
        };
    }

    template<class THandle>
    void ExpectOpaqueHandleContract()
    {
        const THandle invalid;
        const auto first = THandle::FromValues(3, 7);
        const auto same = THandle::FromValues(3, 7);
        const auto otherDomain = THandle::FromValues(4, 7);
        const auto otherValue = THandle::FromValues(3, 8);

        EXPECT_FALSE(static_cast<bool>(invalid));
        EXPECT_FALSE(static_cast<bool>(THandle::FromValues(0, 7)));
        EXPECT_FALSE(static_cast<bool>(THandle::FromValues(3, 0)));
        EXPECT_TRUE(static_cast<bool>(first));
        EXPECT_EQ(first.DomainValue(), 3U);
        EXPECT_EQ(first.Value(), 7U);
        EXPECT_EQ(first, same);
        EXPECT_NE(first, otherDomain);
        EXPECT_NE(first, otherValue);

        const std::unordered_set<THandle> handles {
            first,
            same,
            otherDomain,
            otherValue
        };
        EXPECT_EQ(handles.size(), 3U);
    }

    TEST(RenderingHandles, EveryPublicResourceUsesOpaqueDomainIdentity)
    {
        ExpectOpaqueHandleContract<TGE::Texture2D>();
        ExpectOpaqueHandleContract<TGE::TextureView2D>();
        ExpectOpaqueHandleContract<TGE::Sampler>();
        ExpectOpaqueHandleContract<TGE::ShaderProgram>();
        ExpectOpaqueHandleContract<TGE::Material2D>();
        ExpectOpaqueHandleContract<TGE::MaterialInstance2D>();
        ExpectOpaqueHandleContract<TGE::RenderTarget>();
    }

    TEST(RenderingBlendState, OpaqueDisablesBlendingAndWritesEveryChannel)
    {
        constexpr auto state = TGE::BlendState::Opaque();

        EXPECT_FALSE(state.enabled);
        EXPECT_EQ(state.color.sourceFactor, TGE::BlendFactor::One);
        EXPECT_EQ(state.color.destinationFactor, TGE::BlendFactor::Zero);
        EXPECT_EQ(state.color.operation, TGE::BlendOperation::Add);
        EXPECT_EQ(state.alpha, state.color);
        EXPECT_EQ(state.writeMask, TGE::ColorWriteMask::All);
    }

    TEST(RenderingBlendState, PremultipliedAndStraightAlphaDifferOnlyAtColorSource)
    {
        constexpr auto premultiplied =
            TGE::BlendState::PremultipliedAlpha();
        constexpr auto straight = TGE::BlendState::StraightAlpha();

        EXPECT_TRUE(premultiplied.enabled);
        EXPECT_TRUE(straight.enabled);
        EXPECT_EQ(
            premultiplied.color.sourceFactor,
            TGE::BlendFactor::One);
        EXPECT_EQ(
            straight.color.sourceFactor,
            TGE::BlendFactor::SourceAlpha);
        EXPECT_EQ(
            premultiplied.color.destinationFactor,
            TGE::BlendFactor::OneMinusSourceAlpha);
        EXPECT_EQ(
            straight.color.destinationFactor,
            TGE::BlendFactor::OneMinusSourceAlpha);
        EXPECT_EQ(premultiplied.alpha, straight.alpha);
        EXPECT_EQ(
            premultiplied.writeMask,
            TGE::ColorWriteMask::All);
        EXPECT_EQ(straight.writeMask, TGE::ColorWriteMask::All);
    }

    TEST(RenderingBlendState, AdditiveAddsSourceAndDestination)
    {
        constexpr auto state = TGE::BlendState::Additive();
        const TGE::BlendComponent expected {
            .sourceFactor = TGE::BlendFactor::One,
            .destinationFactor = TGE::BlendFactor::One,
            .operation = TGE::BlendOperation::Add
        };

        EXPECT_TRUE(state.enabled);
        EXPECT_EQ(state.color, expected);
        EXPECT_EQ(state.alpha, expected);
        EXPECT_EQ(state.writeMask, TGE::ColorWriteMask::All);
    }

    TEST(RenderingBlendState, MultiplyUsesDestinationChannelsAsFactors)
    {
        constexpr auto state = TGE::BlendState::Multiply();

        EXPECT_TRUE(state.enabled);
        EXPECT_EQ(
            state.color,
            (TGE::BlendComponent {
                .sourceFactor = TGE::BlendFactor::DestinationColor,
                .destinationFactor = TGE::BlendFactor::Zero,
                .operation = TGE::BlendOperation::Add
            }));
        EXPECT_EQ(
            state.alpha,
            (TGE::BlendComponent {
                .sourceFactor = TGE::BlendFactor::DestinationAlpha,
                .destinationFactor = TGE::BlendFactor::Zero,
                .operation = TGE::BlendOperation::Add
            }));
    }

    TEST(RenderingBlendState, ReplaceIsTheExplicitOpaquePreset)
    {
        constexpr auto replace = TGE::BlendState::Replace();
        constexpr auto opaque = TGE::BlendState::Opaque();

        EXPECT_EQ(replace, opaque);
        EXPECT_FALSE(replace.enabled);
    }

    TEST(RenderingDescriptors, TextureAndRenderTargetValidationAcceptsUsableDescriptors)
    {
        const TGE::Texture2DDescriptor texture {
            .extent = { 64, 32 },
            .format = TGE::PixelFormat::Rgba8Srgb,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .sampleCount = TGE::SampleCount::One,
            .usage = TGE::TextureUsage::Sampled |
                TGE::TextureUsage::TransferDestination,
            .label = "sprite atlas"
        };
        const TGE::RenderTargetDescriptor target {
            .extent = { 1280, 720 },
            .colorFormat = TGE::PixelFormat::Bgra8Srgb,
            .sampleCount = TGE::SampleCount::One,
            .presentable = true,
            .label = "main window"
        };
        const TGE::RenderTexture2DDescriptor renderTexture {
            .extent = { 320, 180 },
            .format = TGE::PixelFormat::Rgba8Srgb,
            .mipLevelCount = 1,
            .sampleCount = TGE::SampleCount::One,
            .additionalUsage = TGE::TextureUsage::TransferSource,
            .label = "post process input"
        };

        EXPECT_TRUE(TGE::Validate(texture));
        EXPECT_TRUE(TGE::Validate(target));
        EXPECT_TRUE(TGE::Validate(renderTexture));
    }

    TEST(RenderingDescriptors, EmptyResourceExtentsAreRejected)
    {
        TGE::Texture2DDescriptor texture;
        texture.format = TGE::PixelFormat::Rgba8Srgb;

        TGE::RenderTargetDescriptor target;
        target.colorFormat = TGE::PixelFormat::Rgba8Srgb;

        TGE::RenderTexture2DDescriptor renderTexture;

        for (auto result : {
                 TGE::Validate(texture),
                 TGE::Validate(target),
                 TGE::Validate(renderTexture) })
        {
            ASSERT_FALSE(result);
            EXPECT_EQ(
                result.error().code,
                TGE::RenderingErrorCode::InvalidExtent);
        }
    }

    TEST(RenderingDescriptors, TextureViewsRequireAResourceAndNonemptyRanges)
    {
        TGE::TextureView2DDescriptor descriptor {
            .texture = TGE::Texture2D::FromValues(11, 2),
            .format = TGE::PixelFormat::Rgba8Srgb,
            .aspect = TGE::TextureAspect::Color,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .label = "sprite region"
        };
        EXPECT_TRUE(TGE::Validate(descriptor));

        descriptor.texture = {};
        auto invalidResource = TGE::Validate(descriptor);
        ASSERT_FALSE(invalidResource);
        EXPECT_EQ(
            invalidResource.error().code,
            TGE::RenderingErrorCode::InvalidResource);

        descriptor.texture = TGE::Texture2D::FromValues(11, 2);
        descriptor.mipLevelCount = 0;
        auto emptyMipRange = TGE::Validate(descriptor);
        ASSERT_FALSE(emptyMipRange);
        EXPECT_EQ(
            emptyMipRange.error().code,
            TGE::RenderingErrorCode::InvalidDescriptor);
    }

    TEST(RenderingDescriptors, SamplerLodAndAnisotropyRangesAreValidated)
    {
        EXPECT_TRUE(TGE::Validate(TGE::SamplerDescriptor {}));

        TGE::SamplerDescriptor reversedLod;
        reversedLod.minimumLod = 4.0F;
        reversedLod.maximumLod = 2.0F;
        auto reversedResult = TGE::Validate(reversedLod);
        ASSERT_FALSE(reversedResult);
        EXPECT_EQ(
            reversedResult.error().code,
            TGE::RenderingErrorCode::InvalidDescriptor);

        TGE::SamplerDescriptor zeroAnisotropy;
        zeroAnisotropy.maximumAnisotropy = 0.0F;
        auto anisotropyResult = TGE::Validate(zeroAnisotropy);
        ASSERT_FALSE(anisotropyResult);
        EXPECT_EQ(
            anisotropyResult.error().code,
            TGE::RenderingErrorCode::InvalidDescriptor);
    }

    TEST(RenderingDescriptors, ShaderProgramsRequireACompletePipeline)
    {
        TGE::ShaderProgramDescriptor graphics {
            .stages = {
                MakeShaderStage(TGE::ShaderStage::Vertex),
                MakeShaderStage(TGE::ShaderStage::Fragment)
            },
            .label = "sprite shader"
        };
        EXPECT_TRUE(TGE::Validate(graphics));

        TGE::ShaderProgramDescriptor compute {
            .stages = {
                MakeShaderStage(TGE::ShaderStage::Compute)
            },
            .label = "atlas compute shader"
        };
        EXPECT_TRUE(TGE::Validate(compute));

        graphics.stages.pop_back();
        auto incompleteGraphics = TGE::Validate(graphics);
        ASSERT_FALSE(incompleteGraphics);
        EXPECT_EQ(
            incompleteGraphics.error().code,
            TGE::RenderingErrorCode::InvalidDescriptor);

        graphics.stages.push_back(
            MakeShaderStage(TGE::ShaderStage::Vertex));
        auto duplicateStage = TGE::Validate(graphics);
        ASSERT_FALSE(duplicateStage);
        EXPECT_EQ(
            duplicateStage.error().code,
            TGE::RenderingErrorCode::InvalidDescriptor);

        compute.stages.front().bytecode.resize(3);
        auto incompleteWord = TGE::Validate(compute);
        ASSERT_FALSE(incompleteWord);
        EXPECT_EQ(
            incompleteWord.error().code,
            TGE::RenderingErrorCode::InvalidData);
    }

    TEST(RenderingDescriptors, MaterialsRequireAValidShaderHandle)
    {
        TGE::Material2DDescriptor descriptor {
            .shader = TGE::ShaderProgram::FromValues(5, 8),
            .blendState = TGE::BlendState::PremultipliedAlpha(),
            .label = "sprite material"
        };

        EXPECT_TRUE(TGE::Validate(descriptor));

        descriptor.shader = {};
        auto result = TGE::Validate(descriptor);
        ASSERT_FALSE(result);
        EXPECT_EQ(
            result.error().code,
            TGE::RenderingErrorCode::InvalidResource);
    }

    TEST(RenderingDescriptors, MaterialInstanceAcceptsTypedOverrides)
    {
        const auto material = TGE::Material2D::FromValues(5, 1);
        const auto texture = TGE::TextureView2D::FromValues(5, 2);
        const auto sampler = TGE::Sampler::FromValues(5, 3);
        const TGE::MaterialInstance2DDescriptor descriptor {
            .material = material,
            .parameters = {
                {
                    .name = "opacity",
                    .value = 0.75F
                },
                {
                    .name = "atlasSize",
                    .value = std::array<float, 2> { 1024.0F, 512.0F }
                },
                {
                    .name = "tint",
                    .value = TGE::LinearColor {
                        0.8F,
                        0.6F,
                        0.4F,
                        1.0F
                    }
                }
            },
            .textures = {
                {
                    .name = "mainTexture",
                    .texture = texture,
                    .sampler = sampler
                }
            },
            .label = "orange sprite"
        };

        EXPECT_TRUE(TGE::Validate(descriptor));
        EXPECT_EQ(descriptor.material, material);
        ASSERT_EQ(descriptor.parameters.size(), 3U);
        EXPECT_TRUE(std::holds_alternative<float>(
            descriptor.parameters[0].value));
        EXPECT_TRUE((std::holds_alternative<std::array<float, 2>>(
            descriptor.parameters[1].value)));
        EXPECT_TRUE(std::holds_alternative<TGE::LinearColor>(
            descriptor.parameters[2].value));
        ASSERT_EQ(descriptor.textures.size(), 1U);
        EXPECT_EQ(descriptor.textures.front().texture, texture);
        EXPECT_EQ(descriptor.textures.front().sampler, sampler);
    }

    TEST(RenderingDescriptors, MaterialInstanceRejectsAmbiguousOrInvalidOverrides)
    {
        TGE::MaterialInstance2DDescriptor descriptor {
            .material = TGE::Material2D::FromValues(5, 1),
            .parameters = {
                { .name = "opacity", .value = 0.25F },
                { .name = "opacity", .value = 0.75F }
            },
            .textures = {},
            .label = "invalid overrides"
        };

        auto duplicateParameter = TGE::Validate(descriptor);
        ASSERT_FALSE(duplicateParameter);
        EXPECT_EQ(
            duplicateParameter.error().code,
            TGE::RenderingErrorCode::InvalidDescriptor);

        descriptor.parameters.clear();
        descriptor.textures = {
            {
                .name = "mainTexture",
                .texture = TGE::TextureView2D::FromValues(5, 2),
                .sampler = {}
            }
        };
        auto invalidSampler = TGE::Validate(descriptor);
        ASSERT_FALSE(invalidSampler);
        EXPECT_EQ(
            invalidSampler.error().code,
            TGE::RenderingErrorCode::InvalidResource);

        descriptor.material = {};
        descriptor.textures.clear();
        auto invalidMaterial = TGE::Validate(descriptor);
        ASSERT_FALSE(invalidMaterial);
        EXPECT_EQ(
            invalidMaterial.error().code,
            TGE::RenderingErrorCode::InvalidResource);

        descriptor.material = TGE::Material2D::FromValues(5, 1);
        descriptor.textures = {
            {
                .name = "mainTexture",
                .texture = TGE::TextureView2D::FromValues(6, 2),
                .sampler = TGE::Sampler::FromValues(5, 3)
            }
        };
        auto crossDevice = TGE::Validate(descriptor);
        ASSERT_FALSE(crossDevice);
        EXPECT_EQ(
            crossDevice.error().code,
            TGE::RenderingErrorCode::IncompatibleResource);
    }

    TEST(RenderingDescriptors, RenderTextureBundleIsValidOnlyWhenComplete)
    {
        TGE::RenderTexture2D bundle;
        EXPECT_FALSE(static_cast<bool>(bundle));

        bundle.target = TGE::RenderTarget::FromValues(9, 1);
        bundle.texture = TGE::Texture2D::FromValues(9, 2);
        EXPECT_FALSE(static_cast<bool>(bundle));

        bundle.view = TGE::TextureView2D::FromValues(9, 3);
        EXPECT_TRUE(static_cast<bool>(bundle));

        bundle.view = TGE::TextureView2D::FromValues(10, 3);
        EXPECT_FALSE(static_cast<bool>(bundle));
    }
}
