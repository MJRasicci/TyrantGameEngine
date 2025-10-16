#pragma once

#include "LoggingOptions.hpp"
#include "LogMessage.hpp"
#include "LogBuffer.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace TGE {

//===========================================================================//
//=======> GlobalLogger class <==============================================//
//===========================================================================//

class GlobalLogger
{
public:
    GlobalLogger();

    GlobalLogger(LoggingOptions options);

    ~GlobalLogger();

    // Method to enqueue log messages
    void Log(const LogMessage& message);

private:
    void ProcessQueue();

    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::queue<LogMessage> log_queue_;
    std::thread logging_thread_;
    std::atomic<bool> stop_logging_;
    LogBuffer buffer;
    std::ostream log;
    LoggingOptions options;
};

} // namespace TGE