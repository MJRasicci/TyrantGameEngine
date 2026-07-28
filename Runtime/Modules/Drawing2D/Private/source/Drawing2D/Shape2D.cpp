#include "TGE/Drawing2D/Shape2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace TGE
{
    namespace
    {
        bool IsFinite(LinearColor color) noexcept
        {
            return std::isfinite(color.red) &&
                   std::isfinite(color.green) &&
                   std::isfinite(color.blue) &&
                   std::isfinite(color.alpha);
        }

        FloatRect Expand(FloatRect bounds, float amount) noexcept
        {
            const auto normalized = bounds.Normalized();
            return {
                normalized.x - amount,
                normalized.y - amount,
                normalized.width + amount * 2.0F,
                normalized.height + amount * 2.0F
            };
        }

        int Orientation(
            Vector2f first,
            Vector2f second,
            Vector2f third,
            float epsilon) noexcept
        {
            const auto value = Cross(second - first, third - first);
            if (value > epsilon)
            {
                return 1;
            }
            if (value < -epsilon)
            {
                return -1;
            }
            return 0;
        }

        bool OnSegment(
            Vector2f first,
            Vector2f point,
            Vector2f second,
            float epsilon) noexcept
        {
            return point.x >= std::min(first.x, second.x) - epsilon &&
                   point.x <= std::max(first.x, second.x) + epsilon &&
                   point.y >= std::min(first.y, second.y) - epsilon &&
                   point.y <= std::max(first.y, second.y) + epsilon;
        }

        bool SegmentsIntersect(
            Vector2f firstStart,
            Vector2f firstEnd,
            Vector2f secondStart,
            Vector2f secondEnd,
            float epsilon) noexcept
        {
            const auto o1 =
                Orientation(firstStart, firstEnd, secondStart, epsilon);
            const auto o2 =
                Orientation(firstStart, firstEnd, secondEnd, epsilon);
            const auto o3 =
                Orientation(secondStart, secondEnd, firstStart, epsilon);
            const auto o4 =
                Orientation(secondStart, secondEnd, firstEnd, epsilon);

            if (o1 != o2 && o3 != o4)
            {
                return true;
            }
            return (o1 == 0 &&
                    OnSegment(firstStart, secondStart, firstEnd, epsilon)) ||
                   (o2 == 0 &&
                    OnSegment(firstStart, secondEnd, firstEnd, epsilon)) ||
                   (o3 == 0 &&
                    OnSegment(secondStart, firstStart, secondEnd, epsilon)) ||
                   (o4 == 0 &&
                    OnSegment(secondStart, firstEnd, secondEnd, epsilon));
        }

        FloatRect BoundsOf(std::span<const Vector2f> points) noexcept
        {
            Vector2f minimum = points.front();
            Vector2f maximum = points.front();
            for (const auto point : points)
            {
                minimum.x = std::min(minimum.x, point.x);
                minimum.y = std::min(minimum.y, point.y);
                maximum.x = std::max(maximum.x, point.x);
                maximum.y = std::max(maximum.y, point.y);
            }
            return FloatRect::FromMinMax(minimum, maximum);
        }
    }

    bool StrokeStyle2D::IsValid() const noexcept
    {
        const auto validJoin = [this]
        {
            switch (join)
            {
            case StrokeJoin2D::Miter:
            case StrokeJoin2D::Bevel:
            case StrokeJoin2D::Round:
                return true;
            }
            return false;
        };
        const auto validCap = [this]
        {
            switch (cap)
            {
            case StrokeCap2D::Butt:
            case StrokeCap2D::Square:
            case StrokeCap2D::Round:
                return true;
            }
            return false;
        };

        return validJoin() &&
               validCap() &&
               std::isfinite(width) &&
               width > 0.0F &&
               IsFinite(color) &&
               std::isfinite(miterLimit) &&
               miterLimit >= 1.0F;
    }

    bool ShapeStyle2D::IsValid() const noexcept
    {
        return IsFinite(fill) &&
               (!stroke || stroke->IsValid()) &&
               (fillEnabled || stroke.has_value());
    }

    float ShapeStyle2D::BoundsOutset() const noexcept
    {
        if (!stroke || !stroke->IsValid())
        {
            return 0.0F;
        }

        const auto halfWidth = stroke->width * 0.5F;
        return stroke->join == StrokeJoin2D::Miter
            ? halfWidth * stroke->miterLimit
            : halfWidth;
    }

    bool Rectangle2D::IsValid() const noexcept
    {
        return size.IsFinite() &&
               size.x > 0.0F &&
               size.y > 0.0F &&
               std::isfinite(cornerRadius) &&
               cornerRadius >= 0.0F &&
               cornerRadius <= std::min(size.x, size.y) * 0.5F &&
               style.IsValid();
    }

    FloatRect Rectangle2D::LocalBounds() const noexcept
    {
        return Expand(
            { 0.0F, 0.0F, size.x, size.y },
            style.BoundsOutset());
    }

    bool Circle2D::IsValid() const noexcept
    {
        return std::isfinite(radius) &&
               radius > 0.0F &&
               style.IsValid();
    }

    FloatRect Circle2D::LocalBounds() const noexcept
    {
        const auto outset = style.BoundsOutset();
        return {
            -radius - outset,
            -radius - outset,
            (radius + outset) * 2.0F,
            (radius + outset) * 2.0F
        };
    }

    Shape2DResult<ConvexPolygon2D> ConvexPolygon2D::Create(
        std::span<const Vector2f> sourcePoints,
        ShapeStyle2D style,
        float epsilon)
    {
        if (!style.IsValid())
        {
            return std::unexpected(Shape2DError {
                .code = Shape2DErrorCode::InvalidStyle,
                .message = "A convex polygon requires a valid shape style."
            });
        }
        if (sourcePoints.size() < 3)
        {
            return std::unexpected(Shape2DError {
                .code = Shape2DErrorCode::TooFewPoints,
                .message = "A convex polygon requires at least three points."
            });
        }
        if (!std::isfinite(epsilon) || epsilon <= 0.0F)
        {
            return std::unexpected(Shape2DError {
                .code = Shape2DErrorCode::Degenerate,
                .message = "Convex-polygon epsilon must be finite and positive."
            });
        }

        std::vector<Vector2f> points(
            sourcePoints.begin(),
            sourcePoints.end());
        for (std::size_t first = 0; first < points.size(); ++first)
        {
            if (!points[first].IsFinite())
            {
                return std::unexpected(Shape2DError {
                    .code = Shape2DErrorCode::NonFinitePoint,
                    .message = "Convex-polygon points must be finite."
                });
            }
            for (std::size_t second = first + 1;
                 second < points.size();
                 ++second)
            {
                if ((points[first] - points[second]).LengthSquared() <=
                    epsilon * epsilon)
                {
                    return std::unexpected(Shape2DError {
                        .code = Shape2DErrorCode::DuplicatePoint,
                        .message =
                            "A convex polygon cannot contain duplicate points."
                    });
                }
            }
        }

        float signedDoubleArea = 0.0F;
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            signedDoubleArea += Cross(
                points[index],
                points[(index + 1) % points.size()]);
        }
        if (!std::isfinite(signedDoubleArea) ||
            std::abs(signedDoubleArea) <= epsilon)
        {
            return std::unexpected(Shape2DError {
                .code = Shape2DErrorCode::Degenerate,
                .message = "A convex polygon must enclose a nonzero area."
            });
        }

        for (std::size_t first = 0; first < points.size(); ++first)
        {
            const auto firstNext = (first + 1) % points.size();
            for (std::size_t second = first + 1;
                 second < points.size();
                 ++second)
            {
                const auto secondNext = (second + 1) % points.size();
                const bool adjacent =
                    first == second ||
                    firstNext == second ||
                    secondNext == first;
                if (!adjacent &&
                    SegmentsIntersect(
                        points[first],
                        points[firstNext],
                        points[second],
                        points[secondNext],
                        epsilon))
                {
                    return std::unexpected(Shape2DError {
                        .code = Shape2DErrorCode::SelfIntersecting,
                        .message =
                            "Convex-polygon edges cannot intersect."
                    });
                }
            }
        }

        int winding = 0;
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            const auto cross = Cross(
                points[(index + 1) % points.size()] - points[index],
                points[(index + 2) % points.size()] -
                    points[(index + 1) % points.size()]);
            if (std::abs(cross) <= epsilon)
            {
                return std::unexpected(Shape2DError {
                    .code = Shape2DErrorCode::Degenerate,
                    .message =
                        "A strictly convex polygon cannot have collinear edges."
                });
            }
            const auto currentWinding = cross > 0.0F ? 1 : -1;
            if (winding == 0)
            {
                winding = currentWinding;
            }
            else if (winding != currentWinding)
            {
                return std::unexpected(Shape2DError {
                    .code = Shape2DErrorCode::NonConvex,
                    .message =
                        "Polygon points must describe a convex perimeter."
                });
            }
        }

        if (signedDoubleArea < 0.0F)
        {
            std::reverse(points.begin(), points.end());
        }

        auto bounds = Expand(
            BoundsOf(points),
            style.BoundsOutset());
        return ConvexPolygon2D(
            std::move(points),
            std::move(style),
            bounds);
    }

    std::span<const Vector2f> ConvexPolygon2D::Points() const noexcept
    {
        return points;
    }

    const ShapeStyle2D& ConvexPolygon2D::Style() const noexcept
    {
        return style;
    }

    bool ConvexPolygon2D::IsValid() const noexcept
    {
        return points.size() >= 3 &&
               style.IsValid() &&
               bounds.IsFinite() &&
               std::ranges::all_of(
                   points,
                   [](Vector2f point)
                   {
                       return point.IsFinite();
                   });
    }

    FloatRect ConvexPolygon2D::LocalBounds() const noexcept
    {
        return bounds;
    }

    ConvexPolygon2D::ConvexPolygon2D(
        std::vector<Vector2f> points,
        ShapeStyle2D style,
        FloatRect bounds)
        : points(std::move(points)),
          style(std::move(style)),
          bounds(bounds)
    {
    }
}
