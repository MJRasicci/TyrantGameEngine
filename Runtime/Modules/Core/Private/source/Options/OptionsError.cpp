#include "TGE/Options/OptionsError.hpp"

#include <format>
#include <utility>

namespace TGE
{
    namespace
    {
        std::string FormatMessage(const OptionsError& error)
        {
            if (error.source.empty())
            {
                return error.message;
            }

            return std::format("{}: {}", error.source, error.message);
        }
    }

    OptionsException::OptionsException(OptionsError errorValue)
        : std::runtime_error(FormatMessage(errorValue)),
          error(std::move(errorValue))
    {
    }

    const OptionsError& OptionsException::Error() const noexcept
    {
        return error;
    }
}
