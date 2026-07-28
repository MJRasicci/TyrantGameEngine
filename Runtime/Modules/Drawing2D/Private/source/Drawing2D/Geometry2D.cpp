#include "TGE/Drawing2D/Geometry2D.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace TGE
{
    namespace
    {
        bool IsFiniteColor(LinearColor color) noexcept
        {
            return std::isfinite(color.red) &&
                   std::isfinite(color.green) &&
                   std::isfinite(color.blue) &&
                   std::isfinite(color.alpha);
        }

        bool IsValidIndexCount(
            PrimitiveTopology topology,
            std::size_t count) noexcept
        {
            switch (topology)
            {
            case PrimitiveTopology::PointList:
                return count >= 1;
            case PrimitiveTopology::LineList:
                return count >= 2 && count % 2 == 0;
            case PrimitiveTopology::LineStrip:
                return count >= 2;
            case PrimitiveTopology::TriangleList:
                return count >= 3 && count % 3 == 0;
            case PrimitiveTopology::TriangleStrip:
                return count >= 3;
            }
            return false;
        }
    }

    bool Vertex2D::IsFinite() const noexcept
    {
        return position.IsFinite() &&
               textureCoordinate.IsFinite() &&
               IsFiniteColor(color);
    }

    Geometry2DResult<Geometry2D> Geometry2D::Create(
        std::vector<Vertex2D> vertices,
        std::vector<std::uint32_t> indices,
        PrimitiveTopology topology)
    {
        if (vertices.empty())
        {
            return std::unexpected(Geometry2DError {
                .code = Geometry2DErrorCode::NoVertices,
                .message = "Indexed 2D geometry requires vertices."
            });
        }
        if (indices.empty())
        {
            return std::unexpected(Geometry2DError {
                .code = Geometry2DErrorCode::NoIndices,
                .message = "Indexed 2D geometry requires indices."
            });
        }
        if (!IsValidIndexCount(topology, indices.size()))
        {
            return std::unexpected(Geometry2DError {
                .code = Geometry2DErrorCode::InvalidIndexCount,
                .message =
                    "The index count does not match the primitive topology."
            });
        }

        for (const auto& vertex : vertices)
        {
            if (!vertex.IsFinite())
            {
                return std::unexpected(Geometry2DError {
                    .code = Geometry2DErrorCode::NonFiniteVertex,
                    .message = "Geometry2D vertices must be finite."
                });
            }
        }
        for (const auto index : indices)
        {
            if (index >= vertices.size())
            {
                return std::unexpected(Geometry2DError {
                    .code = Geometry2DErrorCode::IndexOutOfRange,
                    .message =
                        "A Geometry2D index does not reference a vertex."
                });
            }
        }

        Vector2f minimum = vertices.front().position;
        Vector2f maximum = vertices.front().position;
        for (const auto& vertex : vertices)
        {
            minimum.x = std::min(minimum.x, vertex.position.x);
            minimum.y = std::min(minimum.y, vertex.position.y);
            maximum.x = std::max(maximum.x, vertex.position.x);
            maximum.y = std::max(maximum.y, vertex.position.y);
        }

        return Geometry2D(
            std::move(vertices),
            std::move(indices),
            topology,
            FloatRect::FromMinMax(minimum, maximum));
    }

    std::span<const Vertex2D> Geometry2D::Vertices() const noexcept
    {
        return vertices;
    }

    std::span<const std::uint32_t> Geometry2D::Indices() const noexcept
    {
        return indices;
    }

    PrimitiveTopology Geometry2D::Topology() const noexcept
    {
        return topology;
    }

    bool Geometry2D::IsValid() const noexcept
    {
        if (vertices.empty() ||
            !IsValidIndexCount(topology, indices.size()) ||
            !bounds.IsFinite())
        {
            return false;
        }
        return std::ranges::all_of(
                   vertices,
                   [](const Vertex2D& vertex)
                   {
                       return vertex.IsFinite();
                   }) &&
               std::ranges::all_of(
                   indices,
                   [this](std::uint32_t index)
                   {
                       return index < vertices.size();
                   });
    }

    FloatRect Geometry2D::LocalBounds() const noexcept
    {
        return bounds;
    }

    Geometry2D::Geometry2D(
        std::vector<Vertex2D> vertices,
        std::vector<std::uint32_t> indices,
        PrimitiveTopology topology,
        FloatRect bounds)
        : vertices(std::move(vertices)),
          indices(std::move(indices)),
          topology(topology),
          bounds(bounds)
    {
    }
}
