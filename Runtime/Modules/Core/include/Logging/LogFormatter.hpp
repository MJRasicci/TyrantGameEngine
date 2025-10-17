#pragma once

#include "Logging/LogMessage.hpp"

#include <string>
#include <utility>

namespace TGE {

/**
 * @brief Utility responsible for rendering @ref LogMessage instances.
 */
class LogFormatter
{
public:
    /**
     * @brief Constructs a formatter with the provided template.
     * @param formatString Template describing the output layout.
     */
    explicit LogFormatter(std::string formatString =
        "\033[34m{Timestamp}\033[90m [{Level}\033[90m] [\033[33m{SourceContext}\033[90m]\033[0m\n{Message}\n");

    /**
     * @brief Updates the internal template used for formatting.
     * @param formatString Template string that may reference {Timestamp}, {Level},
     *        {SourceContext}, and {Message} placeholders.
     */
    void SetFormatString(std::string formatString);

    /**
     * @brief Retrieves the current format template.
     */
    [[nodiscard]] const std::string& GetFormatString() const noexcept;

    /**
     * @brief Formats the supplied log message into a human-readable string.
     * @param message The log message to format.
     * @return The formatted representation.
     */
    [[nodiscard]] std::string Format(const LogMessage& message) const;

private:
    /**
     * @brief Parses a placeholder token beginning at the specified position.
     */
    std::pair<std::string, std::string> ExtractPropertyAndFormat(const std::string& str, size_t& pos) const;

    std::string formatString;
};

} // namespace TGE

/**
 * @brief Specialises std::formatter to leverage LogFormatter for formatting.
 */
template<>
struct std::formatter<TGE::LogMessage> : std::formatter<std::string_view>
{
    std::string formatString =
        "\033[34m{Timestamp}\033[90m [{Level}\033[90m] [\033[33m{SourceContext}\033[90m]\033[0m\n{Message}\n";

    auto format(const TGE::LogMessage& msg, std::format_context& ctx) const
    {
        TGE::LogFormatter formatter(formatString);
        return std::formatter<string_view>::format(formatter.Format(msg), ctx);
    }
};
