/**
 * @file ITextService2D.hpp
 * @brief Font ownership, shaping, and glyph-atlas boundary for Text2D.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "TGE/Drawing2D/Text2D.hpp"
#include "TGE/Export.hpp"

namespace TGE
{
    struct FontDescriptor2D
    {
        std::vector<std::byte> data;
        std::uint32_t faceIndex {};
        std::string label;
    };

    struct TextLayoutOptions2D
    {
        std::optional<float> maximumWidth;
        TextDirection direction { TextDirection::Automatic };
        TextAlignment alignment { TextAlignment::Start };
    };

    /**
     * @brief Shapes UTF-8 and owns the glyph atlases referenced by layouts.
     *
     * A successful Layout result is ready for RenderPass2D validation and
     * carries atlas texture regions for visible glyphs. The service must keep
     * those texture views live until all Text2D values using them are retired,
     * or return replacement layouts before releasing an atlas.
     */
    class TGE_API ITextService2D
    {
    public:
        virtual ~ITextService2D() = default;

        [[nodiscard]] virtual Text2DResult<Font> CreateFont(
            const FontDescriptor2D& descriptor) = 0;
        [[nodiscard]] virtual bool IsAlive(Font font) const noexcept = 0;
        [[nodiscard]] virtual Text2DResult<TextLayout2D> Layout(
            std::string utf8,
            const TextStyle2D& style,
            const TextLayoutOptions2D& options = {}) = 0;
        [[nodiscard]] virtual Text2DResult<void> DestroyFont(Font font) = 0;
    };
}
