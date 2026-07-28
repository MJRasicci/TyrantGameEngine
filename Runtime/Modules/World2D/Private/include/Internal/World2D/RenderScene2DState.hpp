#pragma once

#include <cstdint>
#include <vector>

#include "TGE/World2D/RenderScene2D.hpp"

namespace TGE
{
    struct RenderScene2D::State
    {
        std::uint64_t revision { 0 };
        std::vector<RenderItem2D> items;
    };
}
