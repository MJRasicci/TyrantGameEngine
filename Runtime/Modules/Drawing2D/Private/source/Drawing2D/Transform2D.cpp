#include "TGE/Drawing2D/Transform2D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace TGE
{
    AffineTransform2D AffineTransform2D::Rotation(
        Angle angle) noexcept
    {
        const auto cosine = std::cos(angle.Radians());
        const auto sine = std::sin(angle.Radians());
        return FromValues(
            cosine, -sine, 0.0F,
            sine, cosine, 0.0F);
    }

    float AffineTransform2D::At(
        std::size_t row,
        std::size_t column) const
    {
        const std::array values {
            m00, m01, m02,
            m10, m11, m12
        };
        if (row > 1 || column > 2)
        {
            throw std::out_of_range(
                "AffineTransform2D indices must address a 2-by-3 matrix.");
        }
        return values[row * 3 + column];
    }

    FloatRect AffineTransform2D::TransformBounds(
        FloatRect bounds) const noexcept
    {
        const auto normalized = bounds.Normalized();
        const std::array corners {
            TransformPoint({ normalized.x, normalized.y }),
            TransformPoint({
                normalized.x + normalized.width,
                normalized.y
            }),
            TransformPoint({
                normalized.x,
                normalized.y + normalized.height
            }),
            TransformPoint({
                normalized.x + normalized.width,
                normalized.y + normalized.height
            })
        };

        Vector2f minimum = corners.front();
        Vector2f maximum = corners.front();
        for (const auto corner : corners)
        {
            minimum.x = std::min(minimum.x, corner.x);
            minimum.y = std::min(minimum.y, corner.y);
            maximum.x = std::max(maximum.x, corner.x);
            maximum.y = std::max(maximum.y, corner.y);
        }
        return FloatRect::FromMinMax(minimum, maximum);
    }

    std::optional<AffineTransform2D> AffineTransform2D::Inverse(
        float determinantEpsilon) const noexcept
    {
        const auto determinant = Determinant();
        if (!IsFinite() ||
            !std::isfinite(determinantEpsilon) ||
            determinantEpsilon < 0.0F ||
            std::abs(determinant) <= determinantEpsilon)
        {
            return std::nullopt;
        }

        const auto inverseDeterminant = 1.0F / determinant;
        const auto inverse00 = m11 * inverseDeterminant;
        const auto inverse01 = -m01 * inverseDeterminant;
        const auto inverse10 = -m10 * inverseDeterminant;
        const auto inverse11 = m00 * inverseDeterminant;
        return FromValues(
            inverse00,
            inverse01,
            -(inverse00 * m02 + inverse01 * m12),
            inverse10,
            inverse11,
            -(inverse10 * m02 + inverse11 * m12));
    }

    bool Transform2D::IsFinite() const noexcept
    {
        return translation.IsFinite() &&
               std::isfinite(rotation.Radians()) &&
               scale.IsFinite() &&
               origin.IsFinite();
    }

    AffineTransform2D Transform2D::Matrix() const noexcept
    {
        return AffineTransform2D::Translation(translation) *
               AffineTransform2D::Rotation(rotation) *
               AffineTransform2D::Scale(scale) *
               AffineTransform2D::Translation(-origin);
    }

    std::optional<AffineTransform2D> Transform2D::InverseMatrix(
        float determinantEpsilon) const noexcept
    {
        return Matrix().Inverse(determinantEpsilon);
    }

    FloatRect Transform2D::TransformBounds(
        FloatRect bounds) const noexcept
    {
        return Matrix().TransformBounds(bounds);
    }
}
