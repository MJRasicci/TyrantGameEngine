/**
 * @file IRenderer2D.hpp
 * @brief Submission boundary for recorded two-dimensional passes.
 */

#pragma once

#include "TGE/Drawing2D/RenderPass2D.hpp"
#include "TGE/Export.hpp"
#include "TGE/Rendering/IRenderingDevice.hpp"

namespace TGE
{
    /**
     * @brief Backend executor for validated RenderPass2D command lists.
     *
     * Submit must call Validate(pass), reject handles that are not live in
     * Device(), and retain referenced resources until GPU execution completes.
     */
    class TGE_API IRenderer2D
    {
    public:
        virtual ~IRenderer2D() = default;

        [[nodiscard]] virtual IRenderingDevice& Device() noexcept = 0;
        [[nodiscard]] virtual const IRenderingDevice& Device()
            const noexcept = 0;
        [[nodiscard]] virtual RenderingResult<void> Submit(
            const RenderPass2D& pass) = 0;
    };
}
