#pragma once

#include "TGE/Logging/LogFormatter.hpp"
#include "TGE/Logging/ILogSink.hpp"

#include <memory>
#include <vector>

namespace TGE {

/**
 * @brief Aggregates configuration required to construct a logging dispatcher.
 */
class LoggingOptions
{
public:
    /**
     * @brief Builds options with the default console sink and colourised formatter.
     */
    LoggingOptions();

    /**
     * @brief Constructs options using a custom format string.
     * @param formatString Template passed to the formatter. See @ref LogFormatter.
     */
    explicit LoggingOptions(std::string formatString);

    /**
     * @brief Adds an output sink that should receive log messages.
     */
    void AddSink(std::shared_ptr<ILogSink> sink);

    /**
     * @brief Removes all registered sinks.
     */
    void ClearSinks();

    /**
     * @brief Retrieves the list of sinks.
     */
    [[nodiscard]] const std::vector<std::shared_ptr<ILogSink>>& GetSinks() const noexcept;

    /**
     * @brief Returns the formatter used by the logging system.
     */
    [[nodiscard]] LogFormatter& GetFormatter() noexcept;

    /**
     * @brief Returns the formatter used by the logging system (const overload).
     */
    [[nodiscard]] const LogFormatter& GetFormatter() const noexcept;

private:
    LogFormatter formatter;
    std::vector<std::shared_ptr<ILogSink>> sinks;
};

} // namespace TGE
