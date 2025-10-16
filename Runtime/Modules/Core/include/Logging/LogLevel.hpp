/**
 * @file LogLevel.hpp
 * @brief Defines the LogLevel enumeration and associated utilities.
 *
 * This file contains the LogLevel enum class, which is used throughout the Tyrant
 * system for controlling and categorizing log output levels. Additionally, it
 * includes utility functions and operators for working with LogLevel values.
 */

#pragma once

#include <ostream>
#include <string>
#include <iosfwd>
#include <string_view>
#include "Export.hpp"

namespace TGE {

//===========================================================================//
//=======> LogLevel Enumeration <============================================//
//===========================================================================//

/**
 * @enum LogLevel
 * @brief Enumerates the levels of logging severity.
 *
 * This enum provides different levels of logging, ranging from None to Fatal,
 * allowing for fine-grained control over logging output. Each level represents
 * a specific severity or importance of the log messages. This categorization helps
 * in filtering logs based on their significance and aids in better diagnostics and
 * analysis of the system's behavior.
 *
 * @details
 * - None: Disables logging, effectively silencing all log outputs. Useful for 
 *         production environments where minimal logging is desirable.
 * - Debug: Provides detailed diagnostic information useful for developers or 
 *          maintainers in understanding the internal working or for debugging purposes.
 * - Info: General informational messages that highlight the progress or state of the
 *         application under normal operation. Typically non-essential but useful for 
 *         general monitoring.
 * - Warning: Indicates situations that are unexpected or unusual but not necessarily 
 *            harmful. Useful for detecting potential issues or areas that might 
 *            require attention in the future.
 * - Error: Denotes significant issues that affect the normal functioning of the application 
 *          but do not cause a complete halt. Often indicates problems that should be 
 *          addressed promptly.
 * - Fatal: Represents critical errors causing the application to terminate. Used to log 
 *          severe issues that prevent the application from further operation or lead to 
 *          a complete system failure.
 */
enum class TGE_API LogLevel
{
    None,     ///< No logging level. Used to disable logging.
    Debug,    ///< Detailed information for diagnosing issues.
    Info,     ///< General messages about system operations.
    Warning,  ///< Notices about unusual or unexpected events.
    Error,    ///< Significant issues affecting system functionality.
    Fatal     ///< Critical problems leading to system failure.
};

//===========================================================================//
//=======> LogLevel Conversion Functions <===================================//
//===========================================================================//

/**
 * @brief Converts LogLevel to a string representation.
 * @param level The LogLevel to convert.
 * @return std::string_view Representing the string form of the LogLevel.
 */
inline std::string_view ToString(const LogLevel level) noexcept
{
    switch (level)
    {
        using enum LogLevel;
    case Debug:   return "Debug";   
    case Info:    return "Info";    
    case Warning: return "Warning"; 
    case Error:   return "Error";   
    case Fatal:   return "Fatal";   
    default:      return "Unknown"; 
    }
}

/**
 * @brief Converts LogLevel to a string representation with ANSI escape codes for colorized rendering to consoles.
 * @param level The LogLevel to convert.
 * @return std::string_view Representing the string form of the LogLevel.
 */
inline std::string_view ToColorizedString(const LogLevel level) noexcept
{
    switch (level)
    {
        using enum LogLevel;
    case Debug:   return "\033[36mDebug";   // Cyan for Debug
    case Info:    return "\033[32mInfo";    // Green for Info
    case Warning: return "\033[33mWarn";    // Yellow for Warning
    case Error:   return "\033[31mError";   // Red for Error
    case Fatal:   return "\033[35mFatal";   // Magenta for Fatal
    default:      return "\033[34mUnknown"; // Blue for Unknown
    }
}

/**
 * @brief Converts a string to the corresponding LogLevel.
 * @param str The string to convert.
 * @return LogLevel The LogLevel represented by the string.
 */
inline LogLevel FromString(const std::string_view str) noexcept
{
    if (str == "Debug") return LogLevel::Debug;
    if (str == "Info") return LogLevel::Info;
    if (str == "Warning") return LogLevel::Warning;
    if (str == "Error") return LogLevel::Error;
    if (str == "Fatal") return LogLevel::Fatal;
    return LogLevel::None;
}

//===========================================================================//
//=======> LogLevel Comparison Operators <===================================//
//===========================================================================//

/**
 * @brief Less than operator for LogLevel.
 * @param lhs Left-hand side LogLevel for comparison.
 * @param rhs Right-hand side LogLevel for comparison.
 * @return constexpr bool True if lhs is lower severity than rhs.
 */
TGE_API constexpr bool operator<(LogLevel lhs, LogLevel rhs) noexcept;

/**
 * @brief Greater than operator for LogLevel.
 * @param lhs Left-hand side LogLevel for comparison.
 * @param rhs Right-hand side LogLevel for comparison.
 * @return constexpr bool True if lhs is higher severity than rhs.
 */
TGE_API constexpr bool operator>(LogLevel lhs, LogLevel rhs) noexcept;

/**
 * @brief Less than or equal to operator for LogLevel.
 * @param lhs Left-hand side LogLevel for comparison.
 * @param rhs Right-hand side LogLevel for comparison.
 * @return constexpr bool True if lhs is lower severity or equal to rhs.
 */
TGE_API constexpr bool operator<=(LogLevel lhs, LogLevel rhs) noexcept;

/**
 * @brief Greater than or equal to operator for LogLevel.
 * @param lhs Left-hand side LogLevel for comparison.
 * @param rhs Right-hand side LogLevel for comparison.
 * @return constexpr bool True if lhs is higher severity or equal to rhs.
 */
TGE_API constexpr bool operator>=(LogLevel lhs, LogLevel rhs) noexcept;

//===========================================================================//
//=======> LogLevel Stream Overload and Utilities <==========================//
//===========================================================================//

/**
 * @brief Overloads the << operator for outputting LogLevel to an ostream.
 * @param os The output stream.
 * @param level The LogLevel to output.
 * @return std::ostream& The modified output stream.
 */
TGE_API std::ostream& operator<<(std::ostream& os, LogLevel level);

/**
 * @brief Gets the next higher LogLevel.
 * @param level The current LogLevel.
 * @return constexpr LogLevel The next higher LogLevel.
 */
TGE_API constexpr LogLevel NextLevel(LogLevel level) noexcept;

/**
 * @brief Gets the previous lower LogLevel.
 * @param level The current LogLevel.
 * @return constexpr LogLevel The previous lower LogLevel.
 */
TGE_API constexpr LogLevel PreviousLevel(LogLevel level) noexcept;

} // namespace TGE
