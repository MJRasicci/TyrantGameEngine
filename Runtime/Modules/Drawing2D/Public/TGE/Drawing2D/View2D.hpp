/**
 * @file View2D.hpp
 * @brief Camera and viewport mapping for retained two-dimensional drawing.
 */

#pragma once

#include <optional>

#include "TGE/Drawing2D/Transform2D.hpp"
#include "TGE/Export.hpp"
#include "TGE/Rendering.hpp"

namespace TGE
{
    /**
     * @brief Orthographic 2D camera in world units.
     *
     * World coordinates use +Y upward. Pixel coordinates use the target's
     * conventional top-left origin and +Y downward.
     */
    struct TGE_API View2D
    {
        Vector2f center {};
        Vector2f size { 1280.0F, 720.0F };
        Angle rotation {};

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] AffineTransform2D ViewToWorld() const noexcept;
        [[nodiscard]] std::optional<AffineTransform2D> WorldToView() const
            noexcept;
        [[nodiscard]] FloatRect VisibleBounds() const noexcept;

        [[nodiscard]] std::optional<Vector2f> MapWorldToPixel(
            Vector2f worldPoint,
            PixelRect viewport) const noexcept;
        [[nodiscard]] std::optional<Vector2f> MapPixelToWorld(
            Vector2f pixelPoint,
            PixelRect viewport) const noexcept;

        bool operator==(const View2D&) const = default;
    };

    /**
     * @brief Normalized top-left target rectangle occupied by one view.
     */
    struct TGE_API Viewport2D
    {
        FloatRect normalized { 0.0F, 0.0F, 1.0F, 1.0F };

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::optional<PixelRect> Resolve(
            Extent2U targetExtent) const noexcept;

        bool operator==(const Viewport2D&) const = default;
    };
}
