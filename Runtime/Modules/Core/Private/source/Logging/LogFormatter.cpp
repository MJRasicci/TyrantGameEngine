#include "TGE/Logging/LogFormatter.hpp"

#include <format>
#include <iterator>
#include <stdexcept>

namespace TGE {

LogFormatter::LogFormatter(std::string formatString)
    : formatString(std::move(formatString))
{
}

void LogFormatter::SetFormatString(std::string newFormatString)
{
    formatString = std::move(newFormatString);
}

const std::string& LogFormatter::GetFormatString() const noexcept
{
    return formatString;
}

std::string LogFormatter::Format(const LogMessage& message) const
{
    std::string temp;

    for (size_t i = 0; i < formatString.length(); ++i)
    {
        if (formatString[i] == '{')
        {
            auto [property, propertyFormat] = ExtractPropertyAndFormat(formatString, i);

            if (property == "Timestamp")
            {
                std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(message.Timestamp));
            }
            else if (property == "Level")
            {
                std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(message.Level));
            }
            else if (property == "SourceContext")
            {
                std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(message.SourceContext));
            }
            else if (property == "Message")
            {
                std::vformat_to(std::back_inserter(temp), propertyFormat, std::make_format_args(message.Body));
            }
            else
            {
                throw std::runtime_error("Unknown property: " + property);
            }
        }
        else
        {
            temp.push_back(formatString[i]);
        }
    }

    return temp;
}

std::pair<std::string, std::string> LogFormatter::ExtractPropertyAndFormat(const std::string& str, size_t& pos) const
{
    size_t start = pos + 1;
    size_t end = str.find('}', start);
    if (end == std::string::npos)
    {
        throw std::runtime_error("Malformed format string: missing '}'");
    }

    size_t colonPos = str.find(':', start);
    std::string property;
    std::string propertyFormat = "{}";

    if (colonPos < end && colonPos != std::string::npos)
    {
        property = str.substr(start, colonPos - start);
        propertyFormat = "{:" + str.substr(colonPos + 1, end - colonPos - 1) + "}";
    }
    else
    {
        property = str.substr(start, end - start);
    }

    pos = end;
    return { property, propertyFormat };
}

} // namespace TGE
