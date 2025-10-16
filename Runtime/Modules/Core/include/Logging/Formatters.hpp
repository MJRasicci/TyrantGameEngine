#pragma once

#include "Export.hpp"
#include "Logging/LogLevel.hpp"
#include "Logging/LogMessage.hpp"
#include <format>
#include <string_view>

template<>
struct std::formatter<TGE::LogLevel> : std::formatter<string_view>
{
    auto format(const TGE::LogLevel level, std::format_context& ctx) const
    {
        std::string temp;
        std::format_to(std::back_inserter(temp), "{}", TGE::ToColorizedString(level));
        return std::formatter<string_view>::format(temp, ctx);
    }
};

template<>
struct std::formatter<TGE::LogMessage> : std::formatter<std::string_view>
{
    std::string formatString = "\033[34m{Timestamp}\033[90m [{Level}\033[90m] [\033[33m{SourceContext}\033[90m]\033[0m\n{Message}\n";

    // Extracts a property and its format from the format string.
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
