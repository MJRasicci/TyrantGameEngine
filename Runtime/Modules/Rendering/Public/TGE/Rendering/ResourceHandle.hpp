#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace TGE
{
    /**
     * @brief Backend-neutral identity for a renderer-owned resource.
     *
     * domain distinguishes renderer/device registries while value identifies a
     * generational slot within that registry. Neither value is a native API
     * handle. A default-constructed handle is invalid.
     */
    template<class TTag>
    class RenderingHandle final
    {
    public:
        constexpr RenderingHandle() noexcept = default;

        [[nodiscard]] static constexpr RenderingHandle FromValues(
            std::uint64_t domain,
            std::uint64_t value) noexcept
        {
            if (domain == 0 || value == 0)
            {
                return {};
            }
            return RenderingHandle(domain, value);
        }

        [[nodiscard]] constexpr std::uint64_t DomainValue() const noexcept
        {
            return domain;
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return domain != 0 && value != 0;
        }

        auto operator<=>(const RenderingHandle&) const = default;

    private:
        constexpr RenderingHandle(
            std::uint64_t domain,
            std::uint64_t value) noexcept
            : domain(domain),
              value(value)
        {
        }

        std::uint64_t domain {};
        std::uint64_t value {};
    };

    struct Texture2DTag;
    struct TextureView2DTag;
    struct SamplerTag;
    struct ShaderProgramTag;
    struct Material2DTag;
    struct MaterialInstance2DTag;
    struct RenderTargetTag;

    using Texture2D = RenderingHandle<Texture2DTag>;
    using TextureView2D = RenderingHandle<TextureView2DTag>;
    using Sampler = RenderingHandle<SamplerTag>;
    using ShaderProgram = RenderingHandle<ShaderProgramTag>;
    using Material2D = RenderingHandle<Material2DTag>;
    using MaterialInstance2D = RenderingHandle<MaterialInstance2DTag>;
    using RenderTarget = RenderingHandle<RenderTargetTag>;
}

template<class TTag>
struct std::hash<TGE::RenderingHandle<TTag>>
{
    std::size_t operator()(
        TGE::RenderingHandle<TTag> handle) const noexcept
    {
        const auto domain = std::hash<std::uint64_t> {}(
            handle.DomainValue());
        const auto value = std::hash<std::uint64_t> {}(handle.Value());
        return domain ^ (value + 0x9e3779b9U + (domain << 6U) +
            (domain >> 2U));
    }
};
