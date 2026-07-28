/**
 * @file Types.hpp
 * @brief Scalar and geometric value types shared by Drawing2D.
 */

#pragma once

#include <cmath>
#include <compare>
#include <numbers>
#include <optional>

namespace TGE
{
    struct Vector2f
    {
        float x { 0.0F };
        float y { 0.0F };

        [[nodiscard]] constexpr bool IsFinite() const noexcept
        {
            return std::isfinite(x) && std::isfinite(y);
        }

        [[nodiscard]] constexpr float LengthSquared() const noexcept
        {
            return x * x + y * y;
        }

        [[nodiscard]] float Length() const noexcept
        {
            return std::sqrt(LengthSquared());
        }

        [[nodiscard]] std::optional<Vector2f> Normalized(
            float epsilon = 1.0e-6F) const noexcept
        {
            const auto length = Length();
            if (!std::isfinite(length) || length <= epsilon)
            {
                return std::nullopt;
            }
            return Vector2f { x / length, y / length };
        }

        constexpr Vector2f& operator+=(Vector2f other) noexcept
        {
            x += other.x;
            y += other.y;
            return *this;
        }

        constexpr Vector2f& operator-=(Vector2f other) noexcept
        {
            x -= other.x;
            y -= other.y;
            return *this;
        }

        constexpr Vector2f& operator*=(float scalar) noexcept
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }

        constexpr Vector2f& operator/=(float scalar) noexcept
        {
            x /= scalar;
            y /= scalar;
            return *this;
        }

        [[nodiscard]] constexpr Vector2f operator-() const noexcept
        {
            return {-x, -y};
        }

        bool operator==(const Vector2f&) const = default;
    };

    [[nodiscard]] constexpr Vector2f operator+(
        Vector2f left,
        Vector2f right) noexcept
    {
        return left += right;
    }

    [[nodiscard]] constexpr Vector2f operator-(
        Vector2f left,
        Vector2f right) noexcept
    {
        return left -= right;
    }

    [[nodiscard]] constexpr Vector2f operator*(
        Vector2f value,
        float scalar) noexcept
    {
        return value *= scalar;
    }

    [[nodiscard]] constexpr Vector2f operator*(
        float scalar,
        Vector2f value) noexcept
    {
        return value *= scalar;
    }

    [[nodiscard]] constexpr Vector2f operator/(
        Vector2f value,
        float scalar) noexcept
    {
        return value /= scalar;
    }

    [[nodiscard]] constexpr Vector2f Hadamard(
        Vector2f left,
        Vector2f right) noexcept
    {
        return { left.x * right.x, left.y * right.y };
    }

    [[nodiscard]] constexpr float Dot(
        Vector2f left,
        Vector2f right) noexcept
    {
        return left.x * right.x + left.y * right.y;
    }

    [[nodiscard]] constexpr float Cross(
        Vector2f left,
        Vector2f right) noexcept
    {
        return left.x * right.y - left.y * right.x;
    }

    /**
     * @brief Axis-aligned rectangle using a minimum corner and signed extent.
     *
     * Call Normalized before geometric tests when negative extents are
     * meaningful to a caller.
     */
    struct FloatRect
    {
        float x { 0.0F };
        float y { 0.0F };
        float width { 0.0F };
        float height { 0.0F };

        [[nodiscard]] static constexpr FloatRect FromMinMax(
            Vector2f minimum,
            Vector2f maximum) noexcept
        {
            return {
                minimum.x,
                minimum.y,
                maximum.x - minimum.x,
                maximum.y - minimum.y
            };
        }

        [[nodiscard]] constexpr bool IsFinite() const noexcept
        {
            return std::isfinite(x) &&
                   std::isfinite(y) &&
                   std::isfinite(width) &&
                   std::isfinite(height);
        }

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return width <= 0.0F || height <= 0.0F;
        }

        [[nodiscard]] constexpr Vector2f Minimum() const noexcept
        {
            const auto maximumX = x + width;
            const auto maximumY = y + height;
            return {
                x < maximumX ? x : maximumX,
                y < maximumY ? y : maximumY
            };
        }

        [[nodiscard]] constexpr Vector2f Maximum() const noexcept
        {
            const auto maximumX = x + width;
            const auto maximumY = y + height;
            return {
                x < maximumX ? maximumX : x,
                y < maximumY ? maximumY : y
            };
        }

        [[nodiscard]] constexpr FloatRect Normalized() const noexcept
        {
            return FromMinMax(Minimum(), Maximum());
        }

        [[nodiscard]] constexpr Vector2f Center() const noexcept
        {
            const auto normalized = Normalized();
            return {
                normalized.x + normalized.width * 0.5F,
                normalized.y + normalized.height * 0.5F
            };
        }

        [[nodiscard]] constexpr bool Contains(
            Vector2f point) const noexcept
        {
            const auto normalized = Normalized();
            return point.x >= normalized.x &&
                   point.x <= normalized.x + normalized.width &&
                   point.y >= normalized.y &&
                   point.y <= normalized.y + normalized.height;
        }

        [[nodiscard]] constexpr bool Intersects(
            FloatRect other) const noexcept
        {
            const auto left = Normalized();
            const auto right = other.Normalized();
            return left.x < right.x + right.width &&
                   left.x + left.width > right.x &&
                   left.y < right.y + right.height &&
                   left.y + left.height > right.y;
        }

        bool operator==(const FloatRect&) const = default;
    };

    class Angle final
    {
    public:
        constexpr Angle() noexcept = default;

        [[nodiscard]] static constexpr Angle FromRadians(
            float value) noexcept
        {
            return Angle(value);
        }

        [[nodiscard]] static constexpr Angle FromDegrees(
            float value) noexcept
        {
            return Angle(value * std::numbers::pi_v<float> / 180.0F);
        }

        [[nodiscard]] constexpr float Radians() const noexcept
        {
            return radians;
        }

        [[nodiscard]] constexpr float Degrees() const noexcept
        {
            return radians * 180.0F / std::numbers::pi_v<float>;
        }

        [[nodiscard]] Angle Normalized() const noexcept
        {
            auto value = std::remainder(
                radians,
                2.0F * std::numbers::pi_v<float>);
            if (value <= -std::numbers::pi_v<float>)
            {
                value += 2.0F * std::numbers::pi_v<float>;
            }
            return Angle(value);
        }

        constexpr Angle& operator+=(Angle other) noexcept
        {
            radians += other.radians;
            return *this;
        }

        constexpr Angle& operator-=(Angle other) noexcept
        {
            radians -= other.radians;
            return *this;
        }

        constexpr Angle& operator*=(float scalar) noexcept
        {
            radians *= scalar;
            return *this;
        }

        auto operator<=>(const Angle&) const = default;

    private:
        explicit constexpr Angle(float radians) noexcept
            : radians(radians)
        {
        }

        float radians { 0.0F };
    };

    [[nodiscard]] constexpr Angle operator+(
        Angle left,
        Angle right) noexcept
    {
        return left += right;
    }

    [[nodiscard]] constexpr Angle operator-(
        Angle left,
        Angle right) noexcept
    {
        return left -= right;
    }

    [[nodiscard]] constexpr Angle operator*(
        Angle angle,
        float scalar) noexcept
    {
        return angle *= scalar;
    }
}
