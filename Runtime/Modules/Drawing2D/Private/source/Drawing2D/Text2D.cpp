#include "TGE/Drawing2D/Text2D.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace TGE
{
    namespace
    {
        struct Utf8Validation
        {
            bool valid { true };
            std::size_t errorOffset {};
        };

        Utf8Validation ValidateUtf8(std::string_view value) noexcept
        {
            const auto* bytes =
                reinterpret_cast<const unsigned char*>(value.data());
            std::size_t offset = 0;
            while (offset < value.size())
            {
                const auto first = bytes[offset];
                std::size_t continuationCount = 0;
                std::uint32_t codePoint = 0;
                std::uint32_t minimum = 0;

                if (first <= 0x7FU)
                {
                    ++offset;
                    continue;
                }
                if ((first & 0xE0U) == 0xC0U)
                {
                    continuationCount = 1;
                    codePoint = first & 0x1FU;
                    minimum = 0x80U;
                }
                else if ((first & 0xF0U) == 0xE0U)
                {
                    continuationCount = 2;
                    codePoint = first & 0x0FU;
                    minimum = 0x800U;
                }
                else if ((first & 0xF8U) == 0xF0U)
                {
                    continuationCount = 3;
                    codePoint = first & 0x07U;
                    minimum = 0x10000U;
                }
                else
                {
                    return { false, offset };
                }

                if (continuationCount > value.size() - offset - 1)
                {
                    return { false, offset };
                }

                for (std::size_t index = 1;
                     index <= continuationCount;
                     ++index)
                {
                    const auto continuation = bytes[offset + index];
                    if ((continuation & 0xC0U) != 0x80U)
                    {
                        return { false, offset + index };
                    }
                    codePoint =
                        (codePoint << 6U) | (continuation & 0x3FU);
                }

                if (codePoint < minimum ||
                    codePoint > 0x10FFFFU ||
                    (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
                {
                    return { false, offset };
                }
                offset += continuationCount + 1;
            }
            return {};
        }

        bool IsUtf8Boundary(
            std::string_view value,
            std::size_t offset) noexcept
        {
            if (offset > value.size())
            {
                return false;
            }
            if (offset == value.size())
            {
                return true;
            }
            const auto byte = static_cast<unsigned char>(value[offset]);
            return (byte & 0xC0U) != 0x80U;
        }

        bool IsFinite(LinearColor color) noexcept
        {
            return std::isfinite(color.red) &&
                   std::isfinite(color.green) &&
                   std::isfinite(color.blue) &&
                   std::isfinite(color.alpha);
        }

        Text2DError InvalidUtf8Error(
            std::string_view value) noexcept
        {
            const auto validation = ValidateUtf8(value);
            return {
                .code = Text2DErrorCode::InvalidUtf8,
                .byteOffset = validation.errorOffset,
                .message = "Text2D requires well-formed UTF-8."
            };
        }
    }

    bool TextStyle2D::IsValid() const noexcept
    {
        return static_cast<bool>(font) &&
               std::isfinite(fontSize) &&
               fontSize > 0.0F &&
               IsFinite(color) &&
               std::isfinite(lineHeight) &&
               lineHeight > 0.0F &&
               std::isfinite(letterSpacing);
    }

    bool TextLayout2D::IsValidFor(
        std::string_view utf8) const noexcept
    {
        if (!bounds.IsFinite() ||
            (maximumWidth &&
             (!std::isfinite(*maximumWidth) || *maximumWidth <= 0.0F)))
        {
            return false;
        }

        switch (direction)
        {
        case TextDirection::Automatic:
        case TextDirection::LeftToRight:
        case TextDirection::RightToLeft:
            break;
        default:
            return false;
        }
        switch (alignment)
        {
        case TextAlignment::Start:
        case TextAlignment::Center:
        case TextAlignment::End:
        case TextAlignment::Justified:
            break;
        default:
            return false;
        }

        for (const auto& glyph : glyphs)
        {
            if (!glyph.font ||
                !glyph.IsFinite() ||
                !IsUtf8Boundary(utf8, glyph.cluster) ||
                (glyph.image && !glyph.image->IsValid()))
            {
                return false;
            }
        }
        return true;
    }

    bool TextLayout2D::IsRenderableFor(
        std::string_view utf8) const noexcept
    {
        if (!ready ||
            !IsValidFor(utf8) ||
            (!utf8.empty() && !bounds.IsEmpty() && glyphs.empty()))
        {
            return false;
        }

        return std::ranges::all_of(
            glyphs,
            [](const ShapedGlyph& glyph)
            {
                const bool hasVisibleArea =
                    glyph.metrics.size.x > 0.0F &&
                    glyph.metrics.size.y > 0.0F;
                return !hasVisibleArea ||
                       (glyph.image && glyph.image->IsValid());
            });
    }

    bool IsValidUtf8(std::string_view value) noexcept
    {
        return ValidateUtf8(value).valid;
    }

    Text2DResult<Text2D> Text2D::Create(
        std::string utf8,
        TextStyle2D style,
        TextLayout2D layout)
    {
        if (!IsValidUtf8(utf8))
        {
            return std::unexpected(InvalidUtf8Error(utf8));
        }
        if (!style.IsValid())
        {
            return std::unexpected(Text2DError {
                .code = Text2DErrorCode::InvalidStyle,
                .message = "Text2D requires a valid font and finite style."
            });
        }
        if (!layout.IsValidFor(utf8))
        {
            return std::unexpected(Text2DError {
                .code = Text2DErrorCode::InvalidLayout,
                .message =
                    "Text2D glyph layout is incompatible with its UTF-8 text."
            });
        }
        return Text2D(
            std::move(utf8),
            std::move(style),
            std::move(layout));
    }

    Text2DResult<void> Text2D::SetUtf8(std::string value)
    {
        if (!IsValidUtf8(value))
        {
            return std::unexpected(InvalidUtf8Error(value));
        }
        utf8 = std::move(value);
        layout.glyphs.clear();
        layout.bounds = {};
        layout.ready = false;
        return {};
    }

    Text2DResult<void> Text2D::SetStyle(TextStyle2D value)
    {
        if (!value.IsValid())
        {
            return std::unexpected(Text2DError {
                .code = Text2DErrorCode::InvalidStyle,
                .message = "Text2D requires a valid font and finite style."
            });
        }
        style = std::move(value);
        layout.glyphs.clear();
        layout.bounds = {};
        layout.ready = false;
        return {};
    }

    Text2DResult<void> Text2D::SetLayout(TextLayout2D value)
    {
        if (!value.IsValidFor(utf8))
        {
            return std::unexpected(Text2DError {
                .code = Text2DErrorCode::InvalidLayout,
                .message =
                    "Text2D glyph layout is incompatible with its UTF-8 text."
            });
        }
        layout = std::move(value);
        return {};
    }

    const std::string& Text2D::Utf8() const noexcept
    {
        return utf8;
    }

    const TextStyle2D& Text2D::Style() const noexcept
    {
        return style;
    }

    const TextLayout2D& Text2D::Layout() const noexcept
    {
        return layout;
    }

    bool Text2D::IsValid() const noexcept
    {
        return IsValidUtf8(utf8) &&
               style.IsValid() &&
               layout.IsValidFor(utf8);
    }

    bool Text2D::IsRenderable() const noexcept
    {
        return IsValid() && layout.IsRenderableFor(utf8);
    }

    FloatRect Text2D::LocalBounds() const noexcept
    {
        return layout.bounds;
    }

    Text2D::Text2D(
        std::string utf8,
        TextStyle2D style,
        TextLayout2D layout)
        : utf8(std::move(utf8)),
          style(std::move(style)),
          layout(std::move(layout))
    {
    }
}
