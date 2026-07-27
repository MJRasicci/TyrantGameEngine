#pragma once

#include <expected>
#include <string>

namespace TGE
{
    enum class WindowErrorCode
    {
        InvalidDescriptor,
        ParentNotFound,
        Unsupported,
        InvalidState,
        WindowDestroyed,
        ManagerStopped,
        PlatformFailure
    };

    struct WindowError
    {
        WindowErrorCode code { WindowErrorCode::PlatformFailure };
        std::string message;

        bool operator==(const WindowError&) const = default;
    };

    /**
     * @brief Successful disposition of a window mutation.
     */
    enum class WindowOperationStatus
    {
        Applied,
        Normalized,
        Cancelled
    };

    using WindowOperationResult =
        std::expected<WindowOperationStatus, WindowError>;
}
