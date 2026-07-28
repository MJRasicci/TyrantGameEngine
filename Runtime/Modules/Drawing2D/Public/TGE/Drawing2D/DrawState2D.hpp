/**
 * @file DrawState2D.hpp
 * @brief Per-command material, sampling, blending, and clipping state.
 */

#pragma once

#include <optional>

#include "TGE/Rendering.hpp"

namespace TGE
{
    /**
     * @brief Per-command overrides shared by every built-in drawable.
     *
     * A null material selects the renderer's built-in material for the
     * drawable. A null sampler selects its default sampler. Explicit samplers
     * apply to a built-in drawable's primary texture; named textures in a
     * custom material retain the samplers in their material bindings.
     *
     * A missing blend override inherits the selected material's blend state.
     * Pixel snapping is evaluated by the submitting renderer because it
     * depends on the target extent and viewport.
     */
    struct DrawState2D
    {
        MaterialInstance2D material;
        Sampler sampler;
        std::optional<BlendState> blend;
        std::optional<PixelRect> scissor;
        bool pixelSnapped { false };

        bool operator==(const DrawState2D&) const = default;
    };
}
