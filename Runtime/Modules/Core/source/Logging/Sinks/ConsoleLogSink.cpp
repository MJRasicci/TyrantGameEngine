#include "Logging/Sinks/ConsoleLogSink.hpp"

#include <ostream>
#include <string_view>

namespace TGE {

ConsoleLogSink::ConsoleLogSink(std::ostream& outputStream)
    : stream(outputStream)
{
}

void ConsoleLogSink::Write(const LogMessage& /*message*/, std::string_view formattedMessage)
{
    std::scoped_lock guard(writeMutex);
    stream << formattedMessage;
    stream.flush();
}

void ConsoleLogSink::Flush()
{
    std::scoped_lock guard(writeMutex);
    stream.flush();
}

} // namespace TGE
