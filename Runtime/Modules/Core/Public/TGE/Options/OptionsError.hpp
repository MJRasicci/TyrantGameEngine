/**
 * @file OptionsError.hpp
 * @brief Error values returned by the typed options pipeline.
 */

#pragma once

#include <expected>
#include <stdexcept>
#include <string>
#include <utility>

#include "TGE/Export.hpp"

namespace TGE
{
    /**
     * @brief Stable error categories produced while constructing option snapshots.
     */
    enum class OptionsErrorCode
    {
        Io,
        Provider,
        Serialization,
        Validation,
        Update
    };

    /**
     * @brief Structured, non-secret diagnostic for an options operation.
     */
    struct OptionsError
    {
        OptionsErrorCode code { OptionsErrorCode::Provider };
        std::string source;
        std::string message;
    };

    /**
     * @brief Expected-style result used throughout the options API.
     */
    template<class T>
    using OptionsResult = std::expected<T, OptionsError>;

    /**
     * @brief Exception used by fluent registration when initial configuration fails.
     *
     * Runtime reload and update APIs return OptionsResult instead, allowing a
     * running application to retain its last-known-good snapshot.
     */
    class TGE_API OptionsException final : public std::runtime_error
    {
    public:
        explicit OptionsException(OptionsError errorValue);

        [[nodiscard]] const OptionsError& Error() const noexcept;

    private:
        OptionsError error;
    };
}
