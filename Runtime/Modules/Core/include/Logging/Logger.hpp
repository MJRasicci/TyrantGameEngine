/**
 * @file Logger.hpp
 * @brief Defines the Logger class for logging messages with different severity levels.
 *
 * This file contains the Logger class template, which is used for logging messages
 * within the Tyrant Game Engine system. It supports logging at various severity levels and formats
 * messages with timestamps and contextual information.
 */

#pragma once

#if defined(__clang__) || defined(__GNUG__)
    #include <cxxabi.h>
#endif

#include "Export.hpp"
#include "LogLevel.hpp"
#include "LogMessage.hpp"
#include "LogBuffer.hpp"
#include "LoggingOptions.hpp"
#include "GlobalLogger.hpp"
#include <iostream>

namespace TGE {

//===========================================================================//
//=======> ILogger interface <===============================================//
//===========================================================================//

class ILogger
{
public:
    virtual void Log(const LogMessage& message) = 0;
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
    Logger(std::shared_ptr<GlobalLogger> globalLogger) : globalLogger(globalLogger)
    {
    }

    /**
     * @brief Logs a LogMessage to the logger.
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
