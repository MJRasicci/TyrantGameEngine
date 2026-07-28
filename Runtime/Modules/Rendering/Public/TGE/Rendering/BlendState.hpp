#pragma once

#include <cstdint>

namespace TGE
{
    enum class BlendFactor
    {
        Zero,
        One,
        SourceColor,
        OneMinusSourceColor,
        DestinationColor,
        OneMinusDestinationColor,
        SourceAlpha,
        OneMinusSourceAlpha,
        DestinationAlpha,
        OneMinusDestinationAlpha
    };

    enum class BlendOperation
    {
        Add,
        Subtract,
        ReverseSubtract,
        Minimum,
        Maximum
    };

    struct BlendComponent
    {
        BlendFactor sourceFactor { BlendFactor::One };
        BlendFactor destinationFactor { BlendFactor::Zero };
        BlendOperation operation { BlendOperation::Add };

        bool operator==(const BlendComponent&) const = default;
    };

    enum class ColorWriteMask : std::uint8_t
    {
        None = 0,
        Red = 1U << 0U,
        Green = 1U << 1U,
        Blue = 1U << 2U,
        Alpha = 1U << 3U,
        All = (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U)
    };

    [[nodiscard]] constexpr ColorWriteMask operator|(
        ColorWriteMask left,
        ColorWriteMask right) noexcept
    {
        return static_cast<ColorWriteMask>(
            static_cast<std::uint8_t>(left) |
            static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr ColorWriteMask operator&(
        ColorWriteMask left,
        ColorWriteMask right) noexcept
    {
        return static_cast<ColorWriteMask>(
            static_cast<std::uint8_t>(left) &
            static_cast<std::uint8_t>(right));
    }

    struct BlendState
    {
        bool enabled { false };
        BlendComponent color {};
        BlendComponent alpha {};
        ColorWriteMask writeMask { ColorWriteMask::All };

        [[nodiscard]] static constexpr BlendState Opaque() noexcept
        {
            return {};
        }

        [[nodiscard]] static constexpr BlendState
            PremultipliedAlpha() noexcept
        {
            return {
                .enabled = true,
                .color = {
                    BlendFactor::One,
                    BlendFactor::OneMinusSourceAlpha,
                    BlendOperation::Add
                },
                .alpha = {
                    BlendFactor::One,
                    BlendFactor::OneMinusSourceAlpha,
                    BlendOperation::Add
                }
            };
        }

        [[nodiscard]] static constexpr BlendState StraightAlpha() noexcept
        {
            return {
                .enabled = true,
                .color = {
                    BlendFactor::SourceAlpha,
                    BlendFactor::OneMinusSourceAlpha,
                    BlendOperation::Add
                },
                .alpha = {
                    BlendFactor::One,
                    BlendFactor::OneMinusSourceAlpha,
                    BlendOperation::Add
                }
            };
        }

        [[nodiscard]] static constexpr BlendState Additive() noexcept
        {
            return {
                .enabled = true,
                .color = {
                    BlendFactor::One,
                    BlendFactor::One,
                    BlendOperation::Add
                },
                .alpha = {
                    BlendFactor::One,
                    BlendFactor::One,
                    BlendOperation::Add
                }
            };
        }

        [[nodiscard]] static constexpr BlendState Multiply() noexcept
        {
            return {
                .enabled = true,
                .color = {
                    BlendFactor::DestinationColor,
                    BlendFactor::Zero,
                    BlendOperation::Add
                },
                .alpha = {
                    BlendFactor::DestinationAlpha,
                    BlendFactor::Zero,
                    BlendOperation::Add
                }
            };
        }

        [[nodiscard]] static constexpr BlendState Replace() noexcept
        {
            return Opaque();
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            const auto validFactor = [](BlendFactor factor)
            {
                switch (factor)
                {
                case BlendFactor::Zero:
                case BlendFactor::One:
                case BlendFactor::SourceColor:
                case BlendFactor::OneMinusSourceColor:
                case BlendFactor::DestinationColor:
                case BlendFactor::OneMinusDestinationColor:
                case BlendFactor::SourceAlpha:
                case BlendFactor::OneMinusSourceAlpha:
                case BlendFactor::DestinationAlpha:
                case BlendFactor::OneMinusDestinationAlpha:
                    return true;
                }
                return false;
            };
            const auto validOperation = [](BlendOperation operation)
            {
                switch (operation)
                {
                case BlendOperation::Add:
                case BlendOperation::Subtract:
                case BlendOperation::ReverseSubtract:
                case BlendOperation::Minimum:
                case BlendOperation::Maximum:
                    return true;
                }
                return false;
            };
            const auto validComponent =
                [&](const BlendComponent& component)
                {
                    return validFactor(component.sourceFactor) &&
                           validFactor(component.destinationFactor) &&
                           validOperation(component.operation);
                };
            constexpr auto knownWriteMask =
                static_cast<std::uint8_t>(ColorWriteMask::All);
            return validComponent(color) &&
                   validComponent(alpha) &&
                   (static_cast<std::uint8_t>(writeMask) &
                       ~knownWriteMask) == 0;
        }

        bool operator==(const BlendState&) const = default;
    };
}
