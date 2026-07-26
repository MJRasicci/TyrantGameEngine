#pragma once

#include "TGE/Export.hpp"
#include "TGE/Logging/LogMessage.hpp"

namespace TGE {

/**
 * @brief Minimal interface for logging backends that consume log messages.
 */
class TGE_API ILogDispatcher
{
public:
    virtual ~ILogDispatcher() = default;

    /**
     * @brief Enqueue a message for processing.
     */
    virtual void Log(const LogMessage& message) = 0;

    /**
     * @brief Flush any buffered messages to their sinks.
     */
    virtual void Flush() = 0;
};

} // namespace TGE
