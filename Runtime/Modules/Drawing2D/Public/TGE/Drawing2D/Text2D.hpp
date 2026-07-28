/**
 * @file Text2D.hpp
 * @brief Semantic UTF-8 text, styling, and externally shaped glyph data.
 */

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "TGE/Drawing2D/Types.hpp"
#include "TGE/Export.hpp"
#include "TGE/Rendering.hpp"

namespace TGE
{
    /**
     * @brief Domain-qualified identity for a font face managed by a text
     * service.
     */
    class Font final
    {
    public:
        constexpr Font() noexcept = default;

        [[nodiscard]] static constexpr Font FromValues(
            std::uint64_t domain,
            std::uint64_t value) noexcept
        {
            if (domain == 0 || value == 0)
            {
                return {};
            }
            return Font(domain, value);
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

        auto operator<=>(const Font&) const = default;

    private:
        explicit constexpr Font(
            std::uint64_t domain,
            std::uint64_t value) noexcept
            : domain(domain),
              value(value)
        {
        }

        std::uint64_t domain {};
        std::uint64_t value {};
    };

    struct GlyphImage2D
    {
        TextureView2D texture;
        PixelRect texelBounds;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return static_cast<bool>(texture) && !texelBounds.IsEmpty();
        }

        bool operator==(const GlyphImage2D&) const = default;
    };

    struct GlyphMetrics
    {
        Vector2f size {};
        Vector2f bearing {};
        Vector2f advance {};

        [[nodiscard]] constexpr bool IsFinite() const noexcept
        {
            return size.IsFinite() &&
                   bearing.IsFinite() &&
                   advance.IsFinite();
        }

        bool operator==(const GlyphMetrics&) const = default;
    };

    /**
     * @brief One glyph placement produced by a Unicode shaping service.
     *
     * cluster is a byte offset into Text2D::Utf8, not a code-point index.
     */
    struct ShapedGlyph
    {
        Font font;
        std::uint32_t glyphIndex {};
        std::size_t cluster {};
        Vector2f position {};
        GlyphMetrics metrics {};
        std::optional<GlyphImage2D> image;

        [[nodiscard]] constexpr bool IsFinite() const noexcept
        {
            return position.IsFinite() && metrics.IsFinite();
        }

        bool operator==(const ShapedGlyph&) const = default;
    };

    enum class TextDirection
    {
        Automatic,
        LeftToRight,
        RightToLeft
    };

    enum class TextAlignment
    {
        Start,
        Center,
        End,
        Justified
    };

    struct TGE_API TextStyle2D
    {
        Font font;
        float fontSize { 16.0F };
        LinearColor color { LinearColor::White() };
        float lineHeight { 1.0F };
        float letterSpacing {};

        [[nodiscard]] bool IsValid() const noexcept;
        bool operator==(const TextStyle2D&) const = default;
    };

    /**
     * @brief Immutable-ready output of an external text layout service.
     */
    struct TGE_API TextLayout2D
    {
        std::vector<ShapedGlyph> glyphs;
        FloatRect bounds {};
        std::optional<float> maximumWidth;
        TextDirection direction { TextDirection::Automatic };
        TextAlignment alignment { TextAlignment::Start };
        bool ready { false };

        [[nodiscard]] bool IsValidFor(std::string_view utf8) const noexcept;
        [[nodiscard]] bool IsRenderableFor(
            std::string_view utf8) const noexcept;
        bool operator==(const TextLayout2D&) const = default;
    };

    enum class Text2DErrorCode
    {
        InvalidFont,
        InvalidUtf8,
        InvalidStyle,
        InvalidLayout,
        BackendFailure
    };

    struct Text2DError
    {
        Text2DErrorCode code { Text2DErrorCode::InvalidUtf8 };
        std::size_t byteOffset {};
        std::string message;

        bool operator==(const Text2DError&) const = default;
    };

    template<class T>
    using Text2DResult = std::expected<T, Text2DError>;

    [[nodiscard]] TGE_API bool IsValidUtf8(
        std::string_view value) noexcept;

    class TGE_API Text2D final
    {
    public:
        Text2D() = default;

        [[nodiscard]] static Text2DResult<Text2D> Create(
            std::string utf8,
            TextStyle2D style,
            TextLayout2D layout = {});

        [[nodiscard]] Text2DResult<void> SetUtf8(std::string utf8);
        [[nodiscard]] Text2DResult<void> SetStyle(TextStyle2D style);
        [[nodiscard]] Text2DResult<void> SetLayout(TextLayout2D layout);

        [[nodiscard]] const std::string& Utf8() const noexcept;
        [[nodiscard]] const TextStyle2D& Style() const noexcept;
        [[nodiscard]] const TextLayout2D& Layout() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsRenderable() const noexcept;
        [[nodiscard]] FloatRect LocalBounds() const noexcept;

        bool operator==(const Text2D&) const = default;

    private:
        Text2D(
            std::string utf8,
            TextStyle2D style,
            TextLayout2D layout);

        std::string utf8;
        TextStyle2D style;
        TextLayout2D layout;
    };
}

template<>
struct std::hash<TGE::Font>
{
    std::size_t operator()(TGE::Font font) const noexcept
    {
        const auto domain = std::hash<std::uint64_t> {}(
            font.DomainValue());
        const auto value = std::hash<std::uint64_t> {}(font.Value());
        return domain ^ (value + 0x9e3779b9U + (domain << 6U) +
            (domain >> 2U));
    }
};
