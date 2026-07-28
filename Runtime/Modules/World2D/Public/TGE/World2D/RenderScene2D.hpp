/**
 * @file RenderScene2D.hpp
 * @brief Immutable render extraction produced by World2D.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "TGE/Export.hpp"
#include "TGE/World2D/World2DId.hpp"
#include "TGE/World2D/World2DTypes.hpp"

namespace TGE
{
    class RenderPass2D;

    struct RenderItem2D
    {
        Node2DId node;
        Visual2DId visual;
        Visual2D value;
        AffineTransform2D worldTransform;
        FloatRect worldBounds;
        DrawState2D drawState;
        DrawOrder2D drawOrder;
        VisibilityMask2D visibilityMask;
        std::uint64_t stableSequence { 0 };

        bool operator==(const RenderItem2D&) const = default;
    };

    /**
     * @brief Immutable snapshot of the renderable state of one World2D.
     *
     * The snapshot owns its CPU-side drawable data. Renderer resources named
     * by its handles remain externally owned and must outlive submission.
     */
    class TGE_API RenderScene2D final
    {
    public:
        RenderScene2D();
        ~RenderScene2D();

        RenderScene2D(const RenderScene2D&) noexcept;
        RenderScene2D& operator=(const RenderScene2D&) noexcept;
        RenderScene2D(RenderScene2D&&) noexcept;
        RenderScene2D& operator=(RenderScene2D&&) noexcept;

        [[nodiscard]] std::uint64_t Revision() const noexcept;
        [[nodiscard]] std::span<const RenderItem2D> Items() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;

        /**
         * @brief Record visible snapshot items into an existing 2D pass.
         *
         * Items retain their deterministic painter order. The pass view is
         * used for conservative bounds culling.
         */
        void Record(
            RenderPass2D& pass,
            VisibilityMask2D visibilityMask = VisibilityMask2D::All()) const;

    private:
        friend class World2D;

        struct State;
        explicit RenderScene2D(std::shared_ptr<const State> state);

        std::shared_ptr<const State> state;
    };
}
