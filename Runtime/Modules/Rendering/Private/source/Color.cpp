#include "TGE/Rendering/Color.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    float DecodeSrgb(std::uint8_t value) noexcept
    {
        const auto encoded = static_cast<float>(value) / 255.0F;
        if (encoded <= 0.04045F)
        {
            return encoded / 12.92F;
        }

        return std::pow((encoded + 0.055F) / 1.055F, 2.4F);
    }

    float Saturate(float value) noexcept
    {
        if (std::isnan(value))
        {
            return 0.0F;
        }
        return std::clamp(value, 0.0F, 1.0F);
    }

    std::uint8_t EncodeSrgb(float value) noexcept
    {
        const auto linear = Saturate(value);
        const auto encoded = linear <= 0.0031308F
            ? linear * 12.92F
            : 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;

        return static_cast<std::uint8_t>(
            std::lround(Saturate(encoded) * 255.0F));
    }

    std::uint8_t EncodeUnorm(float value) noexcept
    {
        return static_cast<std::uint8_t>(
            std::lround(Saturate(value) * 255.0F));
    }
}

namespace TGE
{
    LinearColor LinearColor::FromSrgba8(Srgba8 color) noexcept
    {
        return {
            DecodeSrgb(color.red),
            DecodeSrgb(color.green),
            DecodeSrgb(color.blue),
            static_cast<float>(color.alpha) / 255.0F
        };
    }

    Srgba8 LinearColor::ToSrgba8() const noexcept
    {
        return {
            EncodeSrgb(red),
            EncodeSrgb(green),
            EncodeSrgb(blue),
            EncodeUnorm(alpha)
        };
    }

    LinearColor Srgba8::ToLinear() const noexcept
    {
        return LinearColor::FromSrgba8(*this);
    }
}
