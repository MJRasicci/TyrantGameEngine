/**
 * @file World2DTypes.hpp
 * @brief Retained 2D world descriptors and visual payloads.
 */

#pragma once

#include <compare>
#include <cstdint>
#include <variant>

#include "TGE/Drawing2D/DrawState2D.hpp"
#include "TGE/Drawing2D/Geometry2D.hpp"
#include "TGE/Drawing2D/Shape2D.hpp"
#include "TGE/Drawing2D/Sprite2D.hpp"
#include "TGE/Drawing2D/Text2D.hpp"
#include "TGE/Drawing2D/Transform2D.hpp"

namespace TGE
{
    enum class NodeDestroyPolicy
    {
        DestroySubtree,
        ReparentChildren
    };

    enum class ReparentMode
    {
        KeepLocal,
        KeepWorld
    };

    struct DrawOrder2D
    {
        std::int32_t layer { 0 };
        std::int32_t order { 0 };

        auto operator<=>(const DrawOrder2D&) const = default;
    };

    /**
     * @brief Bit mask used to select retained visuals for a view.
     */
    class VisibilityMask2D final
    {
    public:
        constexpr VisibilityMask2D() noexcept = default;

        [[nodiscard]] static constexpr VisibilityMask2D FromBits(
            std::uint64_t bits) noexcept
        {
            return VisibilityMask2D(bits);
        }

        [[nodiscard]] static constexpr VisibilityMask2D All() noexcept
        {
            return VisibilityMask2D(~std::uint64_t { 0 });
        }

        [[nodiscard]] static constexpr VisibilityMask2D None() noexcept
        {
            return VisibilityMask2D(0);
        }

        [[nodiscard]] constexpr std::uint64_t Bits() const noexcept
        {
            return bits;
        }

        [[nodiscard]] constexpr bool Intersects(
            VisibilityMask2D other) const noexcept
        {
            return (bits & other.bits) != 0;
        }

        auto operator<=>(const VisibilityMask2D&) const = default;

    private:
        explicit constexpr VisibilityMask2D(std::uint64_t bits) noexcept
            : bits(bits)
        {
        }

        std::uint64_t bits { ~std::uint64_t { 0 } };
    };

    /**
     * @brief Common retained properties applied to one visual attachment.
     */
    struct VisualProperties2D
    {
        Transform2D localTransform {};
        DrawState2D drawState {};
        DrawOrder2D drawOrder {};
        VisibilityMask2D visibilityMask { VisibilityMask2D::All() };
        bool visible { true };

        bool operator==(const VisualProperties2D&) const = default;
    };

    using Visual2D = std::variant<
        Sprite2D,
        Text2D,
        Rectangle2D,
        Circle2D,
        ConvexPolygon2D,
        Geometry2D>;
}
