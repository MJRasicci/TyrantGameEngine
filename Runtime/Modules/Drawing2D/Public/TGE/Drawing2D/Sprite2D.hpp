/**
 * @file Sprite2D.hpp
 * @brief Semantic textured-quad drawing data.
 */

#pragma once

#include "TGE/Drawing2D/Types.hpp"
#include "TGE/Export.hpp"
#include "TGE/Rendering.hpp"

namespace TGE
{
    /**
     * @brief Texel-space subregion of a renderer-owned texture view.
     */
    struct TextureRegion2D
    {
        TextureView2D texture;
        PixelRect texelBounds {};

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return static_cast<bool>(texture) &&
                   texelBounds.position.x >= 0 &&
                   texelBounds.position.y >= 0 &&
                   !texelBounds.IsEmpty();
        }

        bool operator==(const TextureRegion2D&) const = default;
    };

    /**
     * @brief Retained semantic sprite independent of placement in a world.
     *
     * origin is measured in local sprite units from the minimum corner.
     */
    struct TGE_API Sprite2D
    {
        TextureRegion2D region;
        Vector2f size {};
        Vector2f origin {};
        LinearColor tint { LinearColor::White() };
        bool flipHorizontal { false };
        bool flipVertical { false };

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] FloatRect LocalBounds() const noexcept;

        bool operator==(const Sprite2D&) const = default;
    };
}
