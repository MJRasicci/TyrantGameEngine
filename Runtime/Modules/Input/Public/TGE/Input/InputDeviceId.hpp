/**
 * @file InputDeviceId.hpp
 * @brief Opaque process-local identity for an input device.
 */

#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace TGE
{
    /**
     * @brief Stable identity assigned to one observed input device.
     *
     * A default-constructed value is invalid. Values never expose native
     * device handles and may be reused only by a later process.
     */
    class InputDeviceId final
    {
    public:
        constexpr InputDeviceId() noexcept = default;

        [[nodiscard]] static constexpr InputDeviceId FromValue(
            std::uint64_t value) noexcept
        {
            return InputDeviceId(value);
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0;
        }

        auto operator<=>(const InputDeviceId&) const = default;

    private:
        explicit constexpr InputDeviceId(std::uint64_t value) noexcept
            : value(value)
        {
        }

        std::uint64_t value { 0 };
    };
}

template<>
struct std::hash<TGE::InputDeviceId>
{
    std::size_t operator()(
        TGE::InputDeviceId id) const noexcept
    {
        return std::hash<std::uint64_t> {}(id.Value());
    }
};
