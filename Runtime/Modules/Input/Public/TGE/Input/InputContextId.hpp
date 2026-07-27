/**
 * @file InputContextId.hpp
 * @brief Opaque process-local identity for an input context.
 */

#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace TGE
{
    /**
     * @brief Stable identity assigned to one input context.
     *
     * A default-constructed value is invalid. The numeric value is diagnostic
     * only and must not be interpreted as a native or platform identifier.
     */
    class InputContextId final
    {
    public:
        constexpr InputContextId() noexcept = default;

        [[nodiscard]] static constexpr InputContextId FromValue(
            std::uint64_t value) noexcept
        {
            return InputContextId(value);
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0;
        }

        auto operator<=>(const InputContextId&) const = default;

    private:
        explicit constexpr InputContextId(std::uint64_t value) noexcept
            : value(value)
        {
        }

        std::uint64_t value { 0 };
    };
}

template<>
struct std::hash<TGE::InputContextId>
{
    std::size_t operator()(
        TGE::InputContextId id) const noexcept
    {
        return std::hash<std::uint64_t> {}(id.Value());
    }
};
