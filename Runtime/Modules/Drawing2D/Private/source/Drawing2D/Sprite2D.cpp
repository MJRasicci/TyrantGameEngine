#include "TGE/Drawing2D/Sprite2D.hpp"

#include <cmath>

namespace TGE
{
    bool Sprite2D::IsValid() const noexcept
    {
        return region.IsValid() &&
               size.IsFinite() &&
               size.x > 0.0F &&
               size.y > 0.0F &&
               origin.IsFinite() &&
               std::isfinite(tint.red) &&
               std::isfinite(tint.green) &&
               std::isfinite(tint.blue) &&
               std::isfinite(tint.alpha);
    }

    FloatRect Sprite2D::LocalBounds() const noexcept
    {
        return {
            -origin.x,
            -origin.y,
            size.x,
            size.y
        };
    }
}
