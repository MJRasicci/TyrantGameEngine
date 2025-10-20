#include "TGE/Logging/LoggingOptions.hpp"

#include "TGE/Logging/Sinks/ConsoleLogSink.hpp"

#include <utility>

namespace TGE {

LoggingOptions::LoggingOptions()
    : formatter()
{
    AddSink(std::make_shared<ConsoleLogSink>());
}

LoggingOptions::LoggingOptions(std::string formatString)
    : formatter(std::move(formatString))
{
    AddSink(std::make_shared<ConsoleLogSink>());
}

void LoggingOptions::AddSink(std::shared_ptr<ILogSink> sink)
{
    if (sink)
    {
        sinks.emplace_back(std::move(sink));
    }
}

void LoggingOptions::ClearSinks()
{
    sinks.clear();
}

const std::vector<std::shared_ptr<ILogSink>>& LoggingOptions::GetSinks() const noexcept
{
    return sinks;
}

LogFormatter& LoggingOptions::GetFormatter() noexcept
{
    return formatter;
}

const LogFormatter& LoggingOptions::GetFormatter() const noexcept
{
    return formatter;
}

} // namespace TGE
