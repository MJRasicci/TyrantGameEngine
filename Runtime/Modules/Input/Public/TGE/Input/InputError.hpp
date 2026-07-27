/**
 * @file InputError.hpp
 * @brief Result and error contracts for input operations.
 */

#pragma once

#include <expected>
#include <string>

namespace TGE
{
    enum class InputErrorCode
    {
        InvalidDescriptor,
        ContextNotFound,
        Unsupported,
        InvalidState,
        ContextDestroyed,
        ManagerStopped,
        PlatformFailure
    };

    struct InputError
    {
        InputErrorCode code { InputErrorCode::PlatformFailure };
        std::string message;

        bool operator==(const InputError&) const = default;
    };

    /**
     * @brief Successful disposition of an input-context mutation.
     */
    enum class InputOperationStatus
    {
        Applied,
        Normalized,
        Cancelled
    };

    using InputOperationResult =
        std::expected<InputOperationStatus, InputError>;
}
