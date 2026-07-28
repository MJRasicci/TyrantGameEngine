/**
 * @file Shape2D.hpp
 * @brief Semantic filled and stroked two-dimensional primitives.
 */

#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "TGE/Drawing2D/Types.hpp"
#include "TGE/Export.hpp"
#include "TGE/Rendering.hpp"

namespace TGE
{
    enum class StrokeJoin2D
    {
        Miter,
        Bevel,
        Round
    };

    enum class StrokeCap2D
    {
        Butt,
        Square,
        Round
    };

    struct TGE_API StrokeStyle2D
    {
        float width { 1.0F };
        LinearColor color { LinearColor::White() };
        StrokeJoin2D join { StrokeJoin2D::Miter };
        StrokeCap2D cap { StrokeCap2D::Butt };
        float miterLimit { 4.0F };

        [[nodiscard]] bool IsValid() const noexcept;
        bool operator==(const StrokeStyle2D&) const = default;
    };

    struct TGE_API ShapeStyle2D
    {
        LinearColor fill { LinearColor::White() };
        bool fillEnabled { true };
        std::optional<StrokeStyle2D> stroke;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] float BoundsOutset() const noexcept;
        bool operator==(const ShapeStyle2D&) const = default;
    };

    struct TGE_API Rectangle2D
    {
        Vector2f size {};
        float cornerRadius {};
        ShapeStyle2D style {};

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] FloatRect LocalBounds() const noexcept;
        bool operator==(const Rectangle2D&) const = default;
    };

    struct TGE_API Circle2D
    {
        float radius {};
        ShapeStyle2D style {};

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] FloatRect LocalBounds() const noexcept;
        bool operator==(const Circle2D&) const = default;
    };

    enum class Shape2DErrorCode
    {
        InvalidStyle,
        TooFewPoints,
        NonFinitePoint,
        DuplicatePoint,
        Degenerate,
        NonConvex,
        SelfIntersecting
    };

    struct Shape2DError
    {
        Shape2DErrorCode code { Shape2DErrorCode::Degenerate };
        std::string message;

        bool operator==(const Shape2DError&) const = default;
    };

    template<class T>
    using Shape2DResult = std::expected<T, Shape2DError>;

    /**
     * @brief Validated, strictly convex, counterclockwise polygon.
     */
    class TGE_API ConvexPolygon2D final
    {
    public:
        [[nodiscard]] static Shape2DResult<ConvexPolygon2D> Create(
            std::span<const Vector2f> points,
            ShapeStyle2D style = {},
            float epsilon = 1.0e-5F);

        [[nodiscard]] std::span<const Vector2f> Points() const noexcept;
        [[nodiscard]] const ShapeStyle2D& Style() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] FloatRect LocalBounds() const noexcept;

        bool operator==(const ConvexPolygon2D&) const = default;

    private:
        ConvexPolygon2D(
            std::vector<Vector2f> points,
            ShapeStyle2D style,
            FloatRect bounds);

        std::vector<Vector2f> points;
        ShapeStyle2D style;
        FloatRect bounds;
    };
}
