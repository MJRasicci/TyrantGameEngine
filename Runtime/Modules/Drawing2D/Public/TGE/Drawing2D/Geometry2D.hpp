/**
 * @file Geometry2D.hpp
 * @brief Validated indexed geometry for custom two-dimensional drawing.
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "TGE/Drawing2D/Types.hpp"
#include "TGE/Export.hpp"
#include "TGE/Rendering.hpp"

namespace TGE
{
    struct TGE_API Vertex2D
    {
        Vector2f position {};
        Vector2f textureCoordinate {};
        LinearColor color { LinearColor::White() };

        [[nodiscard]] bool IsFinite() const noexcept;
        bool operator==(const Vertex2D&) const = default;
    };

    enum class PrimitiveTopology
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };

    enum class Geometry2DErrorCode
    {
        NoVertices,
        NoIndices,
        NonFiniteVertex,
        IndexOutOfRange,
        InvalidIndexCount
    };

    struct Geometry2DError
    {
        Geometry2DErrorCode code { Geometry2DErrorCode::NoVertices };
        std::string message;

        bool operator==(const Geometry2DError&) const = default;
    };

    template<class T>
    using Geometry2DResult = std::expected<T, Geometry2DError>;

    class TGE_API Geometry2D final
    {
    public:
        [[nodiscard]] static Geometry2DResult<Geometry2D> Create(
            std::vector<Vertex2D> vertices,
            std::vector<std::uint32_t> indices,
            PrimitiveTopology topology = PrimitiveTopology::TriangleList);

        [[nodiscard]] std::span<const Vertex2D> Vertices() const noexcept;
        [[nodiscard]] std::span<const std::uint32_t> Indices() const noexcept;
        [[nodiscard]] PrimitiveTopology Topology() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] FloatRect LocalBounds() const noexcept;

        bool operator==(const Geometry2D&) const = default;

    private:
        Geometry2D(
            std::vector<Vertex2D> vertices,
            std::vector<std::uint32_t> indices,
            PrimitiveTopology topology,
            FloatRect bounds);

        std::vector<Vertex2D> vertices;
        std::vector<std::uint32_t> indices;
        PrimitiveTopology topology { PrimitiveTopology::TriangleList };
        FloatRect bounds;
    };
}
