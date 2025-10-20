#pragma once

#include "TGE/Export.hpp"
#include "TGE/Logging/LogLevel.hpp"
#include <format>
#include <string_view>
#include <chrono>

namespace TGE {

//===========================================================================//
//=======> LogMessage struct <===============================================//
//===========================================================================//

/**
 * @brief Captures the structured data associated with a single log entry.
 */
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

// Formatting support is provided by TGE::LogFormatter.

} // namespace TGE
