#include "Logging/GlobalLogger.hpp"

namespace TGE {

GlobalLogger::GlobalLogger() : GlobalLogger(LoggingOptions()) { }

GlobalLogger::GlobalLogger(LoggingOptions options)
    : stop_logging_(false),
        options(options),
        buffer(options.LogFilePath),
        log(&buffer)
{
    log.setf(std::ios::unitbuf);

    // Start the logging thread
    logging_thread_ = std::thread(&GlobalLogger::ProcessQueue, this);
}

GlobalLogger::~GlobalLogger()
{
    // Stop the logging thread and clean up
    stop_logging_ = true;
    cv_.notify_all();
    if (logging_thread_.joinable())
        logging_thread_.join();

    log.flush();
}

// Method to enqueue log messages
void GlobalLogger::Log(const LogMessage& message)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    log_queue_.push(message);
    cv_.notify_one();
}

void GlobalLogger::ProcessQueue()
{
    while (!stop_logging_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this] { return !log_queue_.empty() || stop_logging_; });

        while (!log_queue_.empty()) {
            auto& message = log_queue_.front();
            log << std::format("{}", message) << std::endl;
            log_queue_.pop();
        }
    }
}

} // namespace TGE