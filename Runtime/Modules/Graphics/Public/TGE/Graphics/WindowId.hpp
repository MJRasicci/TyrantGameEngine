#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace TGE
{
    /**
     * @brief Stable, process-local identity assigned to a window.
     *
     * A default-constructed identifier is invalid. Identifiers are opaque to
     * callers and must not be interpreted as native platform handles.
     */
    class WindowId final
    {
    public:
        constexpr WindowId() noexcept = default;

        [[nodiscard]] static constexpr WindowId FromValue(
            std::uint64_t value) noexcept
        {
            return WindowId(value);
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0;
        }

        auto operator<=>(const WindowId&) const = default;

    private:
        explicit constexpr WindowId(std::uint64_t value) noexcept
            : value(value)
        {
        }

        std::uint64_t value { 0 };
    };
}

template<>
struct std::hash<TGE::WindowId>
{
    std::size_t operator()(TGE::WindowId id) const noexcept
    {
        return std::hash<std::uint64_t> {}(id.Value());
    }
};
