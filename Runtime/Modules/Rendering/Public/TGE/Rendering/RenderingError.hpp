#pragma once

#include <expected>
#include <string>

namespace TGE
{
    /**
     * @brief Portable failure categories produced by the Rendering subsystem.
     */
    enum class RenderingErrorCode
    {
        InvalidDescriptor,
        InvalidExtent,
        InvalidData,
        OutOfBounds,
        ArithmeticOverflow,
        UnsupportedFormat,
        Unsupported,
        InvalidResource,
        IncompatibleResource,
        OutOfMemory,
        ShaderCompilationFailed,
        FeedbackLoop,
        TargetUnavailable,
        DeviceLost,
        InvalidState,
        BackendFailure
    };

    struct RenderingError
    {
        RenderingErrorCode code { RenderingErrorCode::BackendFailure };
        std::string message;

        bool operator==(const RenderingError&) const = default;
    };

    template<class T>
    using RenderingResult = std::expected<T, RenderingError>;
}
