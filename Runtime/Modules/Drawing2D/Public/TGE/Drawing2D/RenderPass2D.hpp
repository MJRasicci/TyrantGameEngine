/**
 * @file RenderPass2D.hpp
 * @brief Owning command recording for one ordered 2D render pass.
 */

#pragma once

#include <concepts>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "TGE/Drawing2D/DrawState2D.hpp"
#include "TGE/Drawing2D/Geometry2D.hpp"
#include "TGE/Drawing2D/Shape2D.hpp"
#include "TGE/Drawing2D/Sprite2D.hpp"
#include "TGE/Drawing2D/Text2D.hpp"
#include "TGE/Drawing2D/Transform2D.hpp"
#include "TGE/Drawing2D/View2D.hpp"
#include "TGE/Export.hpp"
#include "TGE/Rendering.hpp"

namespace TGE
{
    struct TGE_API RenderPass2DDescriptor
    {
        RenderTarget target;
        View2D view {};
        Viewport2D viewport {};
        /**
         * A color value clears before drawing. nullopt preserves the target's
         * existing contents. Recorded passes always store their color output.
         */
        std::optional<LinearColor> clearColor {
            LinearColor::Transparent()
        };
        std::string label;

        [[nodiscard]] bool IsValid() const noexcept;
        bool operator==(const RenderPass2DDescriptor&) const = default;
    };

    template<class TDrawable>
    struct DrawCommandPayload2D
    {
        TDrawable drawable;
        AffineTransform2D transform {};
        DrawState2D state {};

        bool operator==(const DrawCommandPayload2D&) const = default;
    };

    using SpriteDrawCommand2D = DrawCommandPayload2D<Sprite2D>;
    using TextDrawCommand2D = DrawCommandPayload2D<Text2D>;
    using RectangleDrawCommand2D = DrawCommandPayload2D<Rectangle2D>;
    using CircleDrawCommand2D = DrawCommandPayload2D<Circle2D>;
    using ConvexPolygonDrawCommand2D =
        DrawCommandPayload2D<ConvexPolygon2D>;
    using GeometryDrawCommand2D = DrawCommandPayload2D<Geometry2D>;

    using DrawCommand2D = std::variant<
        SpriteDrawCommand2D,
        TextDrawCommand2D,
        RectangleDrawCommand2D,
        CircleDrawCommand2D,
        ConvexPolygonDrawCommand2D,
        GeometryDrawCommand2D>;

    enum class RenderPass2DErrorCode
    {
        InvalidDescriptor
    };

    struct RenderPass2DError
    {
        RenderPass2DErrorCode code {
            RenderPass2DErrorCode::InvalidDescriptor
        };
        std::string message;

        bool operator==(const RenderPass2DError&) const = default;
    };

    template<class T>
    using RenderPass2DResult = std::expected<T, RenderPass2DError>;

    class RenderPass2D;

    /**
     * @brief Protocol for custom types that expand into built-in draw commands.
     */
    template<class T>
    concept Drawable2D = requires(
        const T& drawable,
        RenderPass2D& pass,
        const AffineTransform2D& transform,
        const DrawState2D& state)
    {
        { drawable.Record(pass, transform, state) } -> std::same_as<void>;
    };

    /**
     * @brief CPU-side command list for a single target and view.
     *
     * Recording copies each semantic drawable so callers may safely mutate or
     * destroy their source objects after Draw returns. Referenced renderer
     * resources remain externally owned and must stay live through submission;
     * copying a numeric handle does not extend its lifetime.
     */
    class TGE_API RenderPass2D final
    {
    public:
        [[nodiscard]] static RenderPass2DResult<RenderPass2D> Create(
            RenderPass2DDescriptor descriptor);

        [[nodiscard]] const RenderPass2DDescriptor& Descriptor()
            const noexcept;
        [[nodiscard]] RenderTarget Target() const noexcept;
        [[nodiscard]] const View2D& View() const noexcept;
        [[nodiscard]] const Viewport2D& Viewport() const noexcept;
        [[nodiscard]] std::span<const DrawCommand2D> Commands()
            const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] std::size_t CommandCount() const noexcept;
        [[nodiscard]] RenderingResult<void> Validate() const;

        void ClearCommands() noexcept;

        void Draw(
            const Sprite2D& sprite,
            AffineTransform2D transform = {},
            DrawState2D state = {});
        void Draw(
            const Text2D& text,
            AffineTransform2D transform = {},
            DrawState2D state = {});
        void Draw(
            const Rectangle2D& rectangle,
            AffineTransform2D transform = {},
            DrawState2D state = {});
        void Draw(
            const Circle2D& circle,
            AffineTransform2D transform = {},
            DrawState2D state = {});
        void Draw(
            const ConvexPolygon2D& polygon,
            AffineTransform2D transform = {},
            DrawState2D state = {});
        void Draw(
            const Geometry2D& geometry,
            AffineTransform2D transform = {},
            DrawState2D state = {});

        void Draw(
            const Sprite2D& sprite,
            const Transform2D& transform,
            DrawState2D state = {});
        void Draw(
            const Text2D& text,
            const Transform2D& transform,
            DrawState2D state = {});
        void Draw(
            const Rectangle2D& rectangle,
            const Transform2D& transform,
            DrawState2D state = {});
        void Draw(
            const Circle2D& circle,
            const Transform2D& transform,
            DrawState2D state = {});
        void Draw(
            const ConvexPolygon2D& polygon,
            const Transform2D& transform,
            DrawState2D state = {});
        void Draw(
            const Geometry2D& geometry,
            const Transform2D& transform,
            DrawState2D state = {});

        template<Drawable2D TDrawable>
        void Draw(
            const TDrawable& drawable,
            AffineTransform2D transform = {},
            DrawState2D state = {})
        {
            drawable.Record(*this, transform, state);
        }

        template<Drawable2D TDrawable>
        void Draw(
            const TDrawable& drawable,
            const Transform2D& transform,
            DrawState2D state = {})
        {
            drawable.Record(*this, transform.Matrix(), state);
        }

    private:
        explicit RenderPass2D(RenderPass2DDescriptor descriptor);

        template<class TDrawable>
        void Record(
            const TDrawable& drawable,
            AffineTransform2D transform,
            DrawState2D state)
        {
            commands.emplace_back(DrawCommandPayload2D<TDrawable> {
                .drawable = drawable,
                .transform = transform,
                .state = std::move(state)
            });
        }

        RenderPass2DDescriptor descriptor;
        std::vector<DrawCommand2D> commands;
    };
}
