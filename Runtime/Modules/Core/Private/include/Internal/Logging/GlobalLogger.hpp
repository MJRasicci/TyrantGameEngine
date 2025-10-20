#pragma once

#include "TGE/Logging/ILogDispatcher.hpp"
#include "TGE/Logging/LoggingOptions.hpp"
#include "TGE/Logging/LogMessage.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace TGE {

/**
 * @brief Central asynchronous dispatcher that forwards log messages to registered sinks.
 */
class GlobalLogger : public ILogDispatcher
{
public:
    /**
     * @brief Creates a logger with default options (console sink, coloured output).
     */
    GlobalLogger();

    /**
     * @brief Creates a logger using custom configuration.
     */
    explicit GlobalLogger(LoggingOptions options);

    /**
     * @brief Stops the worker thread and flushes outstanding messages.
     */
    ~GlobalLogger();

    /**
     * @brief Enqueues a message for asynchronous processing.
     */
    void Log(const LogMessage& message) override;

    /**
     * @brief Flushes pending messages to all sinks synchronously.
     */
    void Flush() override;

private:
    /**
     * @brief Worker loop that drains the message queue.
     */
    void ProcessQueue();

    /**
     * @brief Forwards a single message to each registered sink.
     */
    void Dispatch(const LogMessage& message);

    std::mutex queueMutex;
    std::condition_variable condition;
    std::queue<LogMessage> messageQueue;
    std::thread worker;
    std::atomic<bool> stopRequested;
    bool isDispatching = false;
    LoggingOptions options;
};

} // namespace TGE
