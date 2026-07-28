#include "TGE/Drawing2D/View2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace TGE
{
    bool View2D::IsValid() const noexcept
    {
        return center.IsFinite() &&
               size.IsFinite() &&
               size.x > 0.0F &&
               size.y > 0.0F &&
               std::isfinite(rotation.Radians());
    }

    AffineTransform2D View2D::ViewToWorld() const noexcept
    {
        return AffineTransform2D::Translation(center) *
               AffineTransform2D::Rotation(rotation);
    }

    std::optional<AffineTransform2D> View2D::WorldToView() const noexcept
    {
        if (!IsValid())
        {
            return std::nullopt;
        }
        return ViewToWorld().Inverse();
    }

    FloatRect View2D::VisibleBounds() const noexcept
    {
        if (!IsValid())
        {
            return {};
        }
        return ViewToWorld().TransformBounds({
            -size.x * 0.5F,
            -size.y * 0.5F,
            size.x,
            size.y
        });
    }

    std::optional<Vector2f> View2D::MapWorldToPixel(
        Vector2f worldPoint,
        PixelRect viewport) const noexcept
    {
        const auto worldToView = WorldToView();
        if (!worldToView ||
            viewport.IsEmpty() ||
            !worldPoint.IsFinite())
        {
            return std::nullopt;
        }

        const auto cameraPoint = worldToView->TransformPoint(worldPoint);
        const auto normalizedX = cameraPoint.x / size.x + 0.5F;
        const auto normalizedY = 0.5F - cameraPoint.y / size.y;
        return Vector2f {
            static_cast<float>(viewport.position.x) +
                normalizedX * static_cast<float>(viewport.extent.width),
            static_cast<float>(viewport.position.y) +
                normalizedY * static_cast<float>(viewport.extent.height)
        };
    }

    std::optional<Vector2f> View2D::MapPixelToWorld(
        Vector2f pixelPoint,
        PixelRect viewport) const noexcept
    {
        if (!IsValid() ||
            viewport.IsEmpty() ||
            !pixelPoint.IsFinite())
        {
            return std::nullopt;
        }

        const auto normalizedX =
            (pixelPoint.x - static_cast<float>(viewport.position.x)) /
            static_cast<float>(viewport.extent.width);
        const auto normalizedY =
            (pixelPoint.y - static_cast<float>(viewport.position.y)) /
            static_cast<float>(viewport.extent.height);
        const Vector2f cameraPoint {
            (normalizedX - 0.5F) * size.x,
            (0.5F - normalizedY) * size.y
        };
        return ViewToWorld().TransformPoint(cameraPoint);
    }

    bool Viewport2D::IsValid() const noexcept
    {
        if (!normalized.IsFinite() ||
            normalized.width <= 0.0F ||
            normalized.height <= 0.0F)
        {
            return false;
        }

        constexpr auto tolerance = 1.0e-6F;
        return normalized.x >= -tolerance &&
               normalized.y >= -tolerance &&
               normalized.x + normalized.width <= 1.0F + tolerance &&
               normalized.y + normalized.height <= 1.0F + tolerance;
    }

    std::optional<PixelRect> Viewport2D::Resolve(
        Extent2U targetExtent) const noexcept
    {
        if (!IsValid() || targetExtent.IsEmpty())
        {
            return std::nullopt;
        }

        const auto left = std::floor(
            static_cast<double>(
                std::clamp(normalized.x, 0.0F, 1.0F)) *
            static_cast<double>(targetExtent.width));
        const auto top = std::floor(
            static_cast<double>(
                std::clamp(normalized.y, 0.0F, 1.0F)) *
            static_cast<double>(targetExtent.height));
        const auto right = std::ceil(
            static_cast<double>(std::clamp(
                normalized.x + normalized.width,
                0.0F,
                1.0F)) *
            static_cast<double>(targetExtent.width));
        const auto bottom = std::ceil(
            static_cast<double>(std::clamp(
                normalized.y + normalized.height,
                0.0F,
                1.0F)) *
            static_cast<double>(targetExtent.height));

        if (right <= left ||
            bottom <= top ||
            left > static_cast<double>(
                std::numeric_limits<std::int32_t>::max()) ||
            top > static_cast<double>(
                std::numeric_limits<std::int32_t>::max()))
        {
            return std::nullopt;
        }

        return PixelRect {
            .position = {
                static_cast<std::int32_t>(left),
                static_cast<std::int32_t>(top)
            },
            .extent = {
                static_cast<std::uint32_t>(right - left),
                static_cast<std::uint32_t>(bottom - top)
            }
        };
    }
}
