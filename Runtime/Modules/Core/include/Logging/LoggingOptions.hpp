#pragma once

#include <string>
#include <chrono>

namespace TGE {

//===========================================================================//
//=======> LoggingOptions struct <===========================================//
//===========================================================================//

struct LoggingOptions
{
public:
    LoggingOptions() : LoggingOptions("{}")
    { }

    LoggingOptions(std::string formatString) : FormatString(formatString)
    {
        LogToFile = false;
        LogToConsole = true;

        std::chrono::zoned_time now{ std::chrono::current_zone(), std::chrono::system_clock::now() };
        LogFilePath = std::format("{:%YY-%MM-%DD %HH:%MM:%SS}.log", now);
    }

    std::string FormatString;
    std::string LogFilePath;
    bool LogToFile;
    bool LogToConsole;
};

} // namespace TGE