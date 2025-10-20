#pragma once

#include "TGE/Logging/LogMessage.hpp"

#include <string_view>

namespace TGE {

/**
 * @brief Interface for log sinks that consume formatted log messages.
 *
 * A log sink represents an output destination for the logging system. Examples include
 * writing to the console, files, in-memory buffers, or remote services. Implementations
 * must be thread-safe because sinks are invoked from the asynchronous logging worker
 * thread.
 */
class ILogSink
{
public:
    /**
     * @brief Virtual destructor to allow derived classes to clean up resources.
     */
    virtual ~ILogSink() = default;

    /**
     * @brief Consume a log message emitted by the logging system.
     * @param message The structured log message instance.
     * @param formattedMessage The message formatted according to the active formatter.
     */
    virtual void Write(const LogMessage& message, std::string_view formattedMessage) = 0;

    /**
     * @brief Flush buffered data to the underlying output destination.
     */
    virtual void Flush() = 0;
};

} // namespace TGE
