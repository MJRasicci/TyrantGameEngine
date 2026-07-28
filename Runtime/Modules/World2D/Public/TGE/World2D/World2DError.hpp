/**
 * @file World2DError.hpp
 * @brief Error contracts for retained 2D world operations.
 */

#pragma once

#include <expected>
#include <string>

namespace TGE
{
    enum class World2DErrorCode
    {
        InvalidDescriptor,
        NodeNotFound,
        VisualNotFound,
        VisualTypeMismatch,
        HierarchyCycle,
        NonInvertibleTransform,
        NonDecomposableTransform
    };

    struct World2DError
    {
        World2DErrorCode code { World2DErrorCode::InvalidDescriptor };
        std::string message;

        bool operator==(const World2DError&) const = default;
    };

    template<class T>
    using World2DResult = std::expected<T, World2DError>;
}
