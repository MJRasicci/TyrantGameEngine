#include "TGE/Drawing2D/RenderPass2D.hpp"

#include <cmath>
#include <type_traits>
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
    }

    bool RenderPass2DDescriptor::IsValid() const noexcept
    {
        return static_cast<bool>(target) &&
               view.IsValid() &&
               viewport.IsValid() &&
               (!clearColor || IsFinite(*clearColor));
    }

    RenderPass2DResult<RenderPass2D> RenderPass2D::Create(
        RenderPass2DDescriptor descriptor)
    {
        if (!descriptor.IsValid())
        {
            return std::unexpected(RenderPass2DError {
                .message =
                    "A 2D render pass requires a target, valid view, valid "
                    "viewport, and finite clear color."
            });
        }
        return RenderPass2D(std::move(descriptor));
    }

    const RenderPass2DDescriptor& RenderPass2D::Descriptor() const noexcept
    {
        return descriptor;
    }

    RenderTarget RenderPass2D::Target() const noexcept
    {
        return descriptor.target;
    }

    const View2D& RenderPass2D::View() const noexcept
    {
        return descriptor.view;
    }

    const Viewport2D& RenderPass2D::Viewport() const noexcept
    {
        return descriptor.viewport;
    }

    std::span<const DrawCommand2D> RenderPass2D::Commands() const noexcept
    {
        return commands;
    }

    bool RenderPass2D::Empty() const noexcept
    {
        return commands.empty();
    }

    std::size_t RenderPass2D::CommandCount() const noexcept
    {
        return commands.size();
    }

    RenderingResult<void> RenderPass2D::Validate() const
    {
        if (!descriptor.IsValid())
        {
            return std::unexpected(RenderingError {
                .code = RenderingErrorCode::InvalidDescriptor,
                .message = "The 2D render-pass descriptor is invalid."
            });
        }

        const auto targetDomain = descriptor.target.DomainValue();
        for (const auto& command : commands)
        {
            auto result = std::visit(
                [targetDomain](const auto& payload) -> RenderingResult<void>
                {
                    if (!payload.transform.IsFinite())
                    {
                        return std::unexpected(RenderingError {
                            .code = RenderingErrorCode::InvalidData,
                            .message =
                                "A 2D draw command has a non-finite transform."
                        });
                    }
                    if (!payload.drawable.IsValid())
                    {
                        return std::unexpected(RenderingError {
                            .code = RenderingErrorCode::InvalidData,
                            .message =
                                "A 2D draw command contains invalid drawable "
                                "data."
                        });
                    }
                    if (payload.state.blend &&
                        !payload.state.blend->IsValid())
                    {
                        return std::unexpected(RenderingError {
                            .code = RenderingErrorCode::InvalidDescriptor,
                            .message =
                                "A 2D draw command has an invalid blend "
                                "override."
                        });
                    }
                    if ((payload.state.material &&
                            payload.state.material.DomainValue() !=
                                targetDomain) ||
                        (payload.state.sampler &&
                            payload.state.sampler.DomainValue() !=
                                targetDomain))
                    {
                        return std::unexpected(RenderingError {
                            .code =
                                RenderingErrorCode::IncompatibleResource,
                            .message =
                                "2D draw-state resources must belong to the "
                                "render target's device."
                        });
                    }

                    using Drawable = std::remove_cvref_t<
                        decltype(payload.drawable)>;
                    if constexpr (std::same_as<Drawable, Sprite2D>)
                    {
                        if (payload.drawable.region.texture.DomainValue() !=
                            targetDomain)
                        {
                            return std::unexpected(RenderingError {
                                .code =
                                    RenderingErrorCode::IncompatibleResource,
                                .message =
                                    "A sprite texture must belong to the "
                                    "render target's device."
                            });
                        }
                    }
                    else if constexpr (std::same_as<Drawable, Text2D>)
                    {
                        if (!payload.drawable.IsRenderable())
                        {
                            return std::unexpected(RenderingError {
                                .code = RenderingErrorCode::InvalidState,
                                .message =
                                    "Text must have a ready glyph layout "
                                    "before submission."
                            });
                        }
                        for (const auto& glyph :
                             payload.drawable.Layout().glyphs)
                        {
                            if (glyph.image &&
                                glyph.image->texture.DomainValue() !=
                                    targetDomain)
                            {
                                return std::unexpected(RenderingError {
                                    .code = RenderingErrorCode::
                                        IncompatibleResource,
                                    .message =
                                        "Glyph textures must belong to the "
                                        "render target's device."
                                });
                            }
                        }
                    }
                    return {};
                },
                command);
            if (!result)
            {
                return result;
            }
        }
        return {};
    }

    void RenderPass2D::ClearCommands() noexcept
    {
        commands.clear();
    }

    void RenderPass2D::Draw(
        const Sprite2D& sprite,
        AffineTransform2D transform,
        DrawState2D state)
    {
        Record(sprite, transform, std::move(state));
    }

    void RenderPass2D::Draw(
        const Text2D& text,
        AffineTransform2D transform,
        DrawState2D state)
    {
        Record(text, transform, std::move(state));
    }

    void RenderPass2D::Draw(
        const Rectangle2D& rectangle,
        AffineTransform2D transform,
        DrawState2D state)
    {
        Record(rectangle, transform, std::move(state));
    }

    void RenderPass2D::Draw(
        const Circle2D& circle,
        AffineTransform2D transform,
        DrawState2D state)
    {
        Record(circle, transform, std::move(state));
    }

    void RenderPass2D::Draw(
        const ConvexPolygon2D& polygon,
        AffineTransform2D transform,
        DrawState2D state)
    {
        Record(polygon, transform, std::move(state));
    }

    void RenderPass2D::Draw(
        const Geometry2D& geometry,
        AffineTransform2D transform,
        DrawState2D state)
    {
        Record(geometry, transform, std::move(state));
    }

    void RenderPass2D::Draw(
        const Sprite2D& sprite,
        const Transform2D& transform,
        DrawState2D state)
    {
        Draw(sprite, transform.Matrix(), std::move(state));
    }

    void RenderPass2D::Draw(
        const Text2D& text,
        const Transform2D& transform,
        DrawState2D state)
    {
        Draw(text, transform.Matrix(), std::move(state));
    }

    void RenderPass2D::Draw(
        const Rectangle2D& rectangle,
        const Transform2D& transform,
        DrawState2D state)
    {
        Draw(rectangle, transform.Matrix(), std::move(state));
    }

    void RenderPass2D::Draw(
        const Circle2D& circle,
        const Transform2D& transform,
        DrawState2D state)
    {
        Draw(circle, transform.Matrix(), std::move(state));
    }

    void RenderPass2D::Draw(
        const ConvexPolygon2D& polygon,
        const Transform2D& transform,
        DrawState2D state)
    {
        Draw(polygon, transform.Matrix(), std::move(state));
    }

    void RenderPass2D::Draw(
        const Geometry2D& geometry,
        const Transform2D& transform,
        DrawState2D state)
    {
        Draw(geometry, transform.Matrix(), std::move(state));
    }

    RenderPass2D::RenderPass2D(
        RenderPass2DDescriptor descriptor)
        : descriptor(std::move(descriptor))
    {
    }
}
