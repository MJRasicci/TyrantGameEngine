/**
 * @file Logger.hpp
 * @brief Defines the Logger class for logging messages with different severity levels.
 *
 * This file contains the Logger class template, which is used for logging messages
 * within the Tyrant system. It supports logging at various severity levels and formats
 * messages with timestamps and contextual information.
 */

#pragma once

#include <iostream>
#include <string>
#include <string_view>
#include <map>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>

#if defined(__clang__) || defined(__GNUG__)
    #include <cxxabi.h>
#endif

#include "Export.hpp"
#include "Formatters.hpp"
#include "LogLevel.hpp"
#include "LogMessage.hpp"
#include "LogBuffer.hpp"
#include "Services/ServiceLocator.hpp"

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

//===========================================================================//
//=======> ILogger interface <===============================================//
//===========================================================================//

class ILogger
{
public:
    virtual void Log(const LogMessage& message) = 0;
};

//===========================================================================//
//=======> GlobalLogger class <==============================================//
//===========================================================================//

class GlobalLogger : public ILogger
{
public:
    GlobalLogger() : GlobalLogger(LoggingOptions()) { }

    GlobalLogger(LoggingOptions options)
        : stop_logging_(false),
          options(options),
          buffer(options.LogFilePath),
          log(&buffer)
    {
        log.setf(std::ios::unitbuf);

        // Start the logging thread
        logging_thread_ = std::thread(&GlobalLogger::ProcessQueue, this);
    }

    ~GlobalLogger()
    {
        // Stop the logging thread and clean up
        stop_logging_ = true;
        cv_.notify_all();
        if (logging_thread_.joinable())
            logging_thread_.join();

        log.flush();
    }

    // Method to enqueue log messages
    void Log(const LogMessage& message)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(message);
        cv_.notify_one();
    }

private:
    void ProcessQueue()
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

    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::queue<LogMessage> log_queue_;
    std::thread logging_thread_;
    std::atomic<bool> stop_logging_;
    LogBuffer buffer;
    std::ostream log;
    LoggingOptions options;
};

//===========================================================================//
//=======> Logger class <====================================================//
//===========================================================================//

/**
 * @class Logger
 * @brief A generic logger class template for logging messages.
 *
 * This class provides templated logging functionality, allowing for logging
 * messages with various levels of severity. It also formats messages with timestamps
 * and source context information.
 *
 * @tparam T The type used to provide context to the logger. Typically, this is the
 * class or component using the logger.
 */
template<class T>
class Logger : public ILogger
{
public:
    Logger(std::shared_ptr<GlobalLogger> global)
    {
        globalLogger = global;
    }

    /**
     * @brief Logs a message with a specified log level.
     * @param level The log level of the message.
     * @param message The message to log.
     */
    void Log(const LogMessage& message)
    {
        globalLogger->Log(message);
    }

    /**
     * @brief Logs an exception as an error.
     * @param exception The exception to log.
     * @details This method logs the message contained in the exception.
     */
    void Log(const std::exception& exception)
    {
        Log(LogMessage { LogLevel::Error, sourceContext, exception.what() });
    }

    /**
     * @brief Logs a debug-level message.
     * @param message The message to log.
     * @details This method logs a message at the Debug level, which is typically used for
     * detailed system information useful in diagnosing problems during development.
     */
    void Debug(std::string_view message)
    {
        Log(LogMessage { LogLevel::Debug, sourceContext, message });
    }

    /**
     * @brief Logs an informational message.
     * @param message The message to log.
     * @details This method logs a message at the Info level, which is typically used for
     * general operational information about the system's state.
     */
    void Info(std::string_view message)
    {
        Log(LogMessage { LogLevel::Info, sourceContext, message });
    }

    /**
     * @brief Logs a warning-level message.
     * @param message The message to log.
     * @details This method logs a message at the Warning level, which is used for
     * situations that are unusual but not necessarily errors, such as minor issues or potential problems.
     */
    void Warning(std::string_view message)
    {
        Log(LogMessage { LogLevel::Warning, sourceContext, message });
    }

    /**
     * @brief Logs an error-level message.
     * @param message The message to log.
     * @details This method logs a message at the Error level, which is used for
     * significant problems in the system that might hinder system operations.
     */
    void Error(std::string_view message)
    {
        Log(LogMessage { LogLevel::Error, sourceContext, message });
    }

    /**
     * @brief Logs a fatal-level message.
     * @param message The message to log.
     * @details This method logs a message at the Fatal level, which is used for
     * severe errors that cause premature termination of the application.
     */
    void Fatal(std::string_view message)
    {
        Log(LogMessage { LogLevel::Fatal, sourceContext, message });
    }

private:
    static const std::string sourceContext;

    std::shared_ptr<GlobalLogger> globalLogger;
};

template <class T>
const std::string Logger<T>::sourceContext = [] {
    #if defined(__clang__) || defined(__GNUG__)
        const char* mangled = typeid(T).name();
        int status = 0;
        std::unique_ptr<char, void(*)(void*)> demangled(
            abi::__cxa_demangle(mangled, nullptr, nullptr, &status),
            std::free
        );
        return status == 0 ? std::string(demangled.get()) : std::string(mangled);
    #else
        return std::string(typeid(T).name()); // MSVC or fallback
    #endif
}();

} // namespace TGE
