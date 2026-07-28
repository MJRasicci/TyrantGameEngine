/**
 * @file World2DId.hpp
 * @brief Opaque identities for retained 2D world nodes and visuals.
 */

#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace TGE
{
    /**
     * @brief Generational identity for one node owned by a World2D.
     *
     * A default-constructed value is invalid. The encoded value is diagnostic
     * only and is meaningful exclusively to the world that issued it.
     */
    class Node2DId final
    {
    public:
        constexpr Node2DId() noexcept = default;

        [[nodiscard]] static constexpr Node2DId FromValues(
            std::uint64_t domain,
            std::uint64_t value) noexcept
        {
            return Node2DId(domain, value);
        }

        [[nodiscard]] constexpr std::uint64_t DomainValue() const noexcept
        {
            return domain;
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return domain != 0 && value != 0;
        }

        auto operator<=>(const Node2DId&) const = default;

    private:
        explicit constexpr Node2DId(
            std::uint64_t domain,
            std::uint64_t value) noexcept
            : domain(domain),
              value(value)
        {
        }

        std::uint64_t domain { 0 };
        std::uint64_t value { 0 };
    };

    /**
     * @brief Generational identity for one visual attached to a World2D node.
     */
    class Visual2DId final
    {
    public:
        constexpr Visual2DId() noexcept = default;

        [[nodiscard]] static constexpr Visual2DId FromValues(
            std::uint64_t domain,
            std::uint64_t value) noexcept
        {
            return Visual2DId(domain, value);
        }

        [[nodiscard]] constexpr std::uint64_t DomainValue() const noexcept
        {
            return domain;
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return domain != 0 && value != 0;
        }

        auto operator<=>(const Visual2DId&) const = default;

    private:
        explicit constexpr Visual2DId(
            std::uint64_t domain,
            std::uint64_t value) noexcept
            : domain(domain),
              value(value)
        {
        }

        std::uint64_t domain { 0 };
        std::uint64_t value { 0 };
    };
}

template<>
struct std::hash<TGE::Node2DId>
{
    std::size_t operator()(TGE::Node2DId id) const noexcept
    {
        const auto domain = std::hash<std::uint64_t> {}(id.DomainValue());
        const auto value = std::hash<std::uint64_t> {}(id.Value());
        return domain ^ (value + 0x9e3779b9U + (domain << 6U) +
            (domain >> 2U));
    }
};

template<>
struct std::hash<TGE::Visual2DId>
{
    std::size_t operator()(TGE::Visual2DId id) const noexcept
    {
        const auto domain = std::hash<std::uint64_t> {}(id.DomainValue());
        const auto value = std::hash<std::uint64_t> {}(id.Value());
        return domain ^ (value + 0x9e3779b9U + (domain << 6U) +
            (domain >> 2U));
    }
};
