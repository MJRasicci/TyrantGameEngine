/**
 * @file Transform2D.hpp
 * @brief Decomposed and affine two-dimensional transforms.
 */

#pragma once

#include <cstddef>
#include <optional>

#include "TGE/Drawing2D/Types.hpp"
#include "TGE/Export.hpp"

namespace TGE
{
    /**
     * @brief Exact affine transform represented as a row-major 2-by-3 matrix.
     *
     * A * B applies B first and then A. This representation preserves shear
     * introduced by composing rotated, non-uniformly scaled hierarchies.
     */
    class TGE_API AffineTransform2D final
    {
    public:
        constexpr AffineTransform2D() noexcept = default;

        [[nodiscard]] static constexpr AffineTransform2D Identity() noexcept
        {
            return {};
        }

        [[nodiscard]] static constexpr AffineTransform2D FromValues(
            float m00,
            float m01,
            float m02,
            float m10,
            float m11,
            float m12) noexcept
        {
            return AffineTransform2D(
                m00,
                m01,
                m02,
                m10,
                m11,
                m12);
        }

        [[nodiscard]] static constexpr AffineTransform2D Translation(
            Vector2f value) noexcept
        {
            return FromValues(
                1.0F, 0.0F, value.x,
                0.0F, 1.0F, value.y);
        }

        [[nodiscard]] static AffineTransform2D Rotation(
            Angle angle) noexcept;

        [[nodiscard]] static constexpr AffineTransform2D Scale(
            Vector2f value) noexcept
        {
            return FromValues(
                value.x, 0.0F, 0.0F,
                0.0F, value.y, 0.0F);
        }

        [[nodiscard]] constexpr float M00() const noexcept { return m00; }
        [[nodiscard]] constexpr float M01() const noexcept { return m01; }
        [[nodiscard]] constexpr float M02() const noexcept { return m02; }
        [[nodiscard]] constexpr float M10() const noexcept { return m10; }
        [[nodiscard]] constexpr float M11() const noexcept { return m11; }
        [[nodiscard]] constexpr float M12() const noexcept { return m12; }

        /**
         * @brief Return one matrix element.
         * @throws std::out_of_range for rows above 1 or columns above 2.
         */
        [[nodiscard]] float At(
            std::size_t row,
            std::size_t column) const;

        [[nodiscard]] constexpr float Determinant() const noexcept
        {
            return m00 * m11 - m01 * m10;
        }

        [[nodiscard]] constexpr bool IsFinite() const noexcept
        {
            return std::isfinite(m00) &&
                   std::isfinite(m01) &&
                   std::isfinite(m02) &&
                   std::isfinite(m10) &&
                   std::isfinite(m11) &&
                   std::isfinite(m12);
        }

        [[nodiscard]] constexpr Vector2f TransformPoint(
            Vector2f point) const noexcept
        {
            return {
                m00 * point.x + m01 * point.y + m02,
                m10 * point.x + m11 * point.y + m12
            };
        }

        [[nodiscard]] constexpr Vector2f TransformVector(
            Vector2f vector) const noexcept
        {
            return {
                m00 * vector.x + m01 * vector.y,
                m10 * vector.x + m11 * vector.y
            };
        }

        [[nodiscard]] FloatRect TransformBounds(
            FloatRect bounds) const noexcept;

        [[nodiscard]] std::optional<AffineTransform2D> Inverse(
            float determinantEpsilon = 1.0e-6F) const noexcept;

        [[nodiscard]] friend constexpr AffineTransform2D operator*(
            const AffineTransform2D& left,
            const AffineTransform2D& right) noexcept
        {
            return FromValues(
                left.m00 * right.m00 + left.m01 * right.m10,
                left.m00 * right.m01 + left.m01 * right.m11,
                left.m00 * right.m02 +
                    left.m01 * right.m12 +
                    left.m02,
                left.m10 * right.m00 + left.m11 * right.m10,
                left.m10 * right.m01 + left.m11 * right.m11,
                left.m10 * right.m02 +
                    left.m11 * right.m12 +
                    left.m12);
        }

        bool operator==(const AffineTransform2D&) const = default;

    private:
        constexpr AffineTransform2D(
            float m00,
            float m01,
            float m02,
            float m10,
            float m11,
            float m12) noexcept
            : m00(m00),
              m01(m01),
              m02(m02),
              m10(m10),
              m11(m11),
              m12(m12)
        {
        }

        float m00 { 1.0F };
        float m01 { 0.0F };
        float m02 { 0.0F };
        float m10 { 0.0F };
        float m11 { 1.0F };
        float m12 { 0.0F };
    };

    /**
     * @brief Convenient translation, rotation, scale, and pivot representation.
     */
    struct TGE_API Transform2D
    {
        Vector2f translation {};
        Angle rotation {};
        Vector2f scale { 1.0F, 1.0F };
        Vector2f origin {};

        [[nodiscard]] bool IsFinite() const noexcept;
        [[nodiscard]] AffineTransform2D Matrix() const noexcept;
        [[nodiscard]] std::optional<AffineTransform2D> InverseMatrix(
            float determinantEpsilon = 1.0e-6F) const noexcept;
        [[nodiscard]] FloatRect TransformBounds(
            FloatRect bounds) const noexcept;

        bool operator==(const Transform2D&) const = default;
    };
}
