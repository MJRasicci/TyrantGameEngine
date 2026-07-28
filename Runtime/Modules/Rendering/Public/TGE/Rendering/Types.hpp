#pragma once

#include <cstdint>

namespace TGE
{
    /**
     * @brief Unsigned two-dimensional extent, normally measured in pixels.
     */
    struct Extent2U
    {
        std::uint32_t width {};
        std::uint32_t height {};

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return width == 0 || height == 0;
        }

        bool operator==(const Extent2U&) const = default;
    };

    /**
     * @brief Signed point in a pixel coordinate space.
     */
    struct PixelPoint
    {
        std::int32_t x {};
        std::int32_t y {};

        bool operator==(const PixelPoint&) const = default;
    };

    /**
     * @brief Pixel rectangle with a signed origin and unsigned extent.
     */
    struct PixelRect
    {
        PixelPoint position {};
        Extent2U extent {};

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return extent.IsEmpty();
        }

        [[nodiscard]] constexpr bool Contains(PixelPoint point) const noexcept
        {
            const auto left = static_cast<std::int64_t>(position.x);
            const auto top = static_cast<std::int64_t>(position.y);
            const auto right = left + static_cast<std::int64_t>(extent.width);
            const auto bottom = top + static_cast<std::int64_t>(extent.height);
            const auto x = static_cast<std::int64_t>(point.x);
            const auto y = static_cast<std::int64_t>(point.y);

            return !IsEmpty() &&
                x >= left &&
                x < right &&
                y >= top &&
                y < bottom;
        }

        bool operator==(const PixelRect&) const = default;
    };
}
