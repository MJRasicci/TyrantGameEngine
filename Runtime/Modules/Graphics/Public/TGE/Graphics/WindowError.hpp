#pragma once

#include <string>

namespace TGE
{
    enum class WindowErrorCode
    {
        InvalidDescriptor,
        ParentNotFound,
        Unsupported,
        PlatformFailure
    };

    struct WindowError
    {
        WindowErrorCode code { WindowErrorCode::PlatformFailure };
        std::string message;
    };
}
