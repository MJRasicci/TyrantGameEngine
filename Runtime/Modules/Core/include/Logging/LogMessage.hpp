#pragma once

#include "Export.hpp"
#include "LogLevel.hpp"
#include <format>
#include <string_view>
#include <chrono>

namespace TGE {

//===========================================================================//
//=======> LogMessage struct <===============================================//
//===========================================================================//

struct LogMessage
{
public:
    LogMessage(LogLevel level, std::string_view sourceContext, std::string_view body) : Level(level), SourceContext(sourceContext), Body(body)
    {
        Timestamp = { std::chrono::current_zone(), std::chrono::system_clock::now() };
    }

    std::chrono::zoned_time<std::chrono::system_clock::duration, const std::chrono::time_zone*> Timestamp;
    LogLevel Level;
    std::string SourceContext;
    std::string Body;
};

} // namespace TGE

/**
 * @struct std::formatter<TGE::LogMessage>
 * @brief Enables `std::format("{:}", msg)` with templated, colorized output.
 * @details
 * Formatting proceeds by scanning `formatString` and expanding placeholders of the form:
 *   `{Property}` or `{Property:format-spec}`
 * where `Property` ∈ {`Timestamp`, `Level`, `SourceContext`, `Message`}.
 * The optional `format-spec` is passed through to `std::format` for that property.
 *
 * Example:
 * @code
 * std::formatter<TGE::LogMessage> f;
 * f.formatString = "[{Level}] {Timestamp:%H:%M:%S} {SourceContext}: {Message}";
 * std::string s = std::format("{}", msg);
 * @endcode
 */
template<>
struct std::formatter<TGE::LogMessage> : std::formatter<std::string_view>
{
    /**
     * @brief Template controlling the rendered layout.
     * @details
     * Default value uses ANSI colors and a two-line layout:
     * `\033[34m{Timestamp}\033[90m [{Level}\033[90m] [\033[33m{SourceContext}\033[90m]\033[0m\n{Message}\n`
     *
     * Supported placeholders:
     *  - `{Timestamp[:<chrono-format>]}` — forwarded to `std::format` with `msg.Timestamp`.
     *  - `{Level}` — forwarded to `std::format` with `msg.Level`.
     *  - `{SourceContext}` — forwarded to `std::format` with `msg.SourceContext`.
     *  - `{Message}` — forwarded to `std::format` with `msg.Body`.
     */
    std::string formatString = "\033[34m{Timestamp}\033[90m [{Level}\033[90m] [\033[33m{SourceContext}\033[90m]\033[0m\n{Message}\n";

    /**
     * @brief Parse a `{Property[:format]}` token from `str` starting at `pos`.
     * @param str The format template string being scanned.
     * @param pos In/out index of the current '{' character. On return, advanced to the matching '}'.
     * @return Pair of `{propertyName, propertyFormat}`, where `propertyFormat` is a valid
     *         `std::format` pattern containing exactly one replacement field, e.g. `"{}"` or `"{:%H:%M:%S}"`.
     * @throws std::runtime_error If a closing `}` is missing or the token is malformed.
     * @remark Internal helper used by `format(const TGE::LogMessage&, std::format_context&)`.
     */
    std::pair<std::string, std::string> extractPropertyAndFormat(const std::string& str, size_t& pos) const
    {
        size_t start = pos + 1;
        size_t end = str.find('}', start);
        if (end == std::string::npos)
        {
            throw std::runtime_error("Malformed format string: missing '}'");
        }

        size_t colonPos = str.find(':', start);
        std::string property, propertyFormat = "{}";

        if (colonPos < end && colonPos != std::string::npos)
        {
            property = str.substr(start, colonPos - start);
            propertyFormat = "{:" + str.substr(colonPos + 1, end - colonPos - 1) + "}";
        } else {
            property = str.substr(start, end - start);
        }

        pos = end;
        return {property, propertyFormat};
    }

    /**
     * @brief Render a `TGE::LogMessage` according to `formatString`.
     * @param msg The log message to render.
     * @param ctx The output format context.
     * @return An iterator to the end of the formatted output.
     * @throws std::runtime_error If an unknown placeholder is encountered.
     * @throws std::format_error  If a constructed `propertyFormat` is invalid for the bound value.
     */
    auto format(const TGE::LogMessage& msg, std::format_context& ctx) const
    {
        std::string temp;

        for (size_t i = 0; i < formatString.length(); i++)
        {
            if (formatString[i] == '{')
            {
                auto [property, propertyFormat] = extractPropertyAndFormat(formatString, i);

                if (property == "Timestamp")
                    std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(msg.Timestamp));
                else if (property == "Level")
                    std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(msg.Level));
                else if (property == "SourceContext")
                    std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(msg.SourceContext));
                else if (property == "Message")
                    std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(msg.Body));
                else
                    throw std::runtime_error("Unknown property: " + property);
            }
            else
            {
                std::format_to(std::back_inserter(temp), "{}", formatString[i]);
            }
        }

        return std::formatter<string_view>::format(temp, ctx);
    }
};