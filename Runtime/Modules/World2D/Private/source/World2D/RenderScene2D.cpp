#include "TGE/World2D/RenderScene2D.hpp"

#include <utility>

#include "Internal/World2D/RenderScene2DState.hpp"
#include "TGE/Drawing2D/RenderPass2D.hpp"

namespace TGE
{
    RenderScene2D::RenderScene2D()
        : state(std::make_shared<State>())
    {
    }

    RenderScene2D::RenderScene2D(std::shared_ptr<const State> state)
        : state(std::move(state))
    {
        if (!this->state)
        {
            this->state = std::make_shared<State>();
        }
    }

    RenderScene2D::~RenderScene2D() = default;
    RenderScene2D::RenderScene2D(const RenderScene2D&) noexcept = default;
    RenderScene2D& RenderScene2D::operator=(
        const RenderScene2D&) noexcept = default;
    RenderScene2D::RenderScene2D(RenderScene2D&& other) noexcept
        : state(other.state)
    {
    }

    RenderScene2D& RenderScene2D::operator=(
        RenderScene2D&& other) noexcept
    {
        state = other.state;
        return *this;
    }

    std::uint64_t RenderScene2D::Revision() const noexcept
    {
        return state->revision;
    }

    std::span<const RenderItem2D> RenderScene2D::Items() const noexcept
    {
        return state->items;
    }

    bool RenderScene2D::Empty() const noexcept
    {
        return state->items.empty();
    }

    void RenderScene2D::Record(
        RenderPass2D& pass,
        VisibilityMask2D visibilityMask) const
    {
        const auto visibleBounds = pass.View().VisibleBounds();
        for (const auto& item : state->items)
        {
            if (!item.visibilityMask.Intersects(visibilityMask) ||
                (item.worldBounds.IsFinite() &&
                    !item.worldBounds.IsEmpty() &&
                    !item.worldBounds.Intersects(visibleBounds)))
            {
                continue;
            }

            std::visit(
                [&pass, &item](const auto& visual)
                {
                    pass.Draw(
                        visual,
                        item.worldTransform,
                        item.drawState);
                },
                item.value);
        }
    }
}
