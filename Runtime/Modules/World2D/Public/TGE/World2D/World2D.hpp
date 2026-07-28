/**
 * @file World2D.hpp
 * @brief Retained transform hierarchy and visual storage for 2D worlds.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "TGE/Export.hpp"
#include "TGE/World2D/RenderScene2D.hpp"
#include "TGE/World2D/World2DError.hpp"
#include "TGE/World2D/World2DId.hpp"
#include "TGE/World2D/World2DTypes.hpp"

namespace TGE
{
    /**
     * @brief Single-writer retained 2D world.
     *
     * Nodes own hierarchy and transform state. Independently identified visual
     * attachments allow one node to carry any number of sprites, text blocks,
     * shapes, or custom geometry records.
     */
    class TGE_API World2D final
    {
    public:
        World2D();
        ~World2D();

        World2D(const World2D&) = delete;
        World2D& operator=(const World2D&) = delete;
        World2D(World2D&&) noexcept;
        World2D& operator=(World2D&&) noexcept;

        [[nodiscard]] World2DResult<Node2DId> CreateNode(
            Transform2D transform = {});
        [[nodiscard]] World2DResult<void> DestroyNode(
            Node2DId node,
            NodeDestroyPolicy policy = NodeDestroyPolicy::DestroySubtree);

        [[nodiscard]] bool Contains(Node2DId node) const noexcept;
        [[nodiscard]] bool Contains(Visual2DId visual) const noexcept;
        [[nodiscard]] std::uint64_t Revision() const noexcept;

        [[nodiscard]] World2DResult<Transform2D> Transform(
            Node2DId node) const;
        [[nodiscard]] World2DResult<AffineTransform2D> WorldTransform(
            Node2DId node) const;
        [[nodiscard]] World2DResult<void> SetTransform(
            Node2DId node,
            Transform2D transform);
        [[nodiscard]] World2DResult<void> SetEnabled(
            Node2DId node,
            bool enabled);
        [[nodiscard]] World2DResult<void> SetParent(
            Node2DId child,
            std::optional<Node2DId> parent,
            ReparentMode mode = ReparentMode::KeepLocal);

        [[nodiscard]] World2DResult<Visual2DId> AddSprite(
            Node2DId node,
            Sprite2D sprite,
            VisualProperties2D properties = {});
        [[nodiscard]] World2DResult<Visual2DId> AddText(
            Node2DId node,
            Text2D text,
            VisualProperties2D properties = {});
        [[nodiscard]] World2DResult<Visual2DId> AddRectangle(
            Node2DId node,
            Rectangle2D rectangle,
            VisualProperties2D properties = {});
        [[nodiscard]] World2DResult<Visual2DId> AddCircle(
            Node2DId node,
            Circle2D circle,
            VisualProperties2D properties = {});
        [[nodiscard]] World2DResult<Visual2DId> AddConvexPolygon(
            Node2DId node,
            ConvexPolygon2D polygon,
            VisualProperties2D properties = {});
        [[nodiscard]] World2DResult<Visual2DId> AddGeometry(
            Node2DId node,
            Geometry2D geometry,
            VisualProperties2D properties = {});

        [[nodiscard]] World2DResult<void> SetVisual(
            Visual2DId visual,
            Visual2D value);
        [[nodiscard]] World2DResult<void> SetVisualProperties(
            Visual2DId visual,
            VisualProperties2D properties);
        [[nodiscard]] World2DResult<VisualProperties2D> VisualProperties(
            Visual2DId visual) const;
        [[nodiscard]] World2DResult<void> RemoveVisual(Visual2DId visual);

        [[nodiscard]] RenderScene2D PublishRenderScene() const;

    private:
        struct State;
        std::unique_ptr<State> state;
    };
}
