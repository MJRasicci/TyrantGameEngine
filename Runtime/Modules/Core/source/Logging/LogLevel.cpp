#include "Logging/LogLevel.hpp"

namespace TGE {

//===========================================================================//
//=======> LogLevel Comparison Operators <===================================//
//===========================================================================//

constexpr bool operator<(LogLevel lhs, LogLevel rhs) noexcept
{
    return static_cast<int>(lhs) < static_cast<int>(rhs);
}

constexpr bool operator>(LogLevel lhs, LogLevel rhs) noexcept
{
    return rhs < lhs;
}

constexpr bool operator<=(LogLevel lhs, LogLevel rhs) noexcept
{
    return !(lhs > rhs);
}

constexpr bool operator>=(LogLevel lhs, LogLevel rhs) noexcept
{
    return !(lhs < rhs);
}

//===========================================================================//
//=======> LogLevel Stream Overload and Utilities <==========================//
//===========================================================================//

std::ostream& operator<<(std::ostream& os, LogLevel level)
{
    return os << ToString(level);
}

constexpr LogLevel NextLevel(LogLevel level) noexcept
{
    return level < LogLevel::Fatal ? static_cast<LogLevel>(static_cast<int>(level) + 1) : LogLevel::Fatal;
}

constexpr LogLevel PreviousLevel(LogLevel level) noexcept
{
    return level > LogLevel::None ? static_cast<LogLevel>(static_cast<int>(level) - 1) : LogLevel::None;
}

} // namespace TGE
