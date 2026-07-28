#pragma once

#include <cstdint>

#include "TGE/Export.hpp"

namespace TGE
{
    struct Srgba8;

    /**
     * @brief Linear-light, straight-alpha color used by rendering operations.
     */
    struct LinearColor
    {
        float red {};
        float green {};
        float blue {};
        float alpha { 1.0F };

        [[nodiscard]] static constexpr LinearColor Transparent() noexcept
        {
            return { 0.0F, 0.0F, 0.0F, 0.0F };
        }

        [[nodiscard]] static constexpr LinearColor Black() noexcept
        {
            return { 0.0F, 0.0F, 0.0F, 1.0F };
        }

        [[nodiscard]] static constexpr LinearColor White() noexcept
        {
            return { 1.0F, 1.0F, 1.0F, 1.0F };
        }

        [[nodiscard]] static TGE_API LinearColor FromSrgba8(
            Srgba8 color) noexcept;

        [[nodiscard]] TGE_API Srgba8 ToSrgba8() const noexcept;

        [[nodiscard]] constexpr LinearColor Premultiplied() const noexcept
        {
            return {
                red * alpha,
                green * alpha,
                blue * alpha,
                alpha
            };
        }

        bool operator==(const LinearColor&) const = default;
    };

    /**
     * @brief Eight-bit nonlinear sRGB color with a linear alpha channel.
     */
    struct Srgba8
    {
        std::uint8_t red {};
        std::uint8_t green {};
        std::uint8_t blue {};
        std::uint8_t alpha { 255 };

        [[nodiscard]] static constexpr Srgba8 Transparent() noexcept
        {
            return { 0, 0, 0, 0 };
        }

        [[nodiscard]] static constexpr Srgba8 Black() noexcept
        {
            return { 0, 0, 0, 255 };
        }

        [[nodiscard]] static constexpr Srgba8 White() noexcept
        {
            return { 255, 255, 255, 255 };
        }

        [[nodiscard]] TGE_API LinearColor ToLinear() const noexcept;

        bool operator==(const Srgba8&) const = default;
    };
}
