/**
 * @file OptionsSerialization.hpp
 * @brief Type-safe JSON serialization for owning option value types.
 */

#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "TGE/Features.hpp"
#include "TGE/Options/OptionsConcepts.hpp"
#include "TGE/Options/OptionsError.hpp"

#if defined(GLZ_REFLECTION26)
    #if GLZ_REFLECTION26 != TGE_HAS_CXX26_OPTIONS_REFLECTION
        #error "GLZ_REFLECTION26 conflicts with TGE's selected options serialization backend."
    #endif
#else
    #define GLZ_REFLECTION26 TGE_HAS_CXX26_OPTIONS_REFLECTION
#endif

#include <glaze/glaze.hpp>

namespace TGE
{
    namespace detail
    {
        struct StrictOptionsJsonSettings : glz::opts
        {
            bool validate_skipped = true;
            bool validate_trailing_whitespace = true;
        };

        inline constexpr auto OptionsJsonReadSettings = []
        {
            auto settings = StrictOptionsJsonSettings {};
            settings.null_terminated = false;
            settings.error_on_unknown_keys = true;
            return settings;
        }();

        inline constexpr auto OptionsJsonWriteSettings = []
        {
            auto settings = glz::opts {};
            settings.skip_null_members = false;
            return settings;
        }();

        inline constexpr auto PrettyOptionsJsonWriteSettings = []
        {
            auto settings = OptionsJsonWriteSettings;
            settings.prettify = true;
            return settings;
        }();

        inline OptionsError MakeSerializationError(
            std::string_view operation,
            const glz::error_ctx& error)
        {
            return OptionsError {
                .code = OptionsErrorCode::Serialization,
                .source = {},
                .message = std::format(
                    "{} failed at byte {}: {}",
                    operation,
                    error.count,
                    glz::format_error(error))
            };
        }
    }

    /**
     * @brief Customization point for serializing an options type.
     *
     * The default implementation uses Glaze aggregate or C++26 reflection.
     * Non-aggregate classes can specialize this template while retaining the
     * same provider and monitor APIs.
     */
    template<OptionsType T>
    struct OptionsSerializer
    {
        static OptionsResult<std::string> Serialize(
            const T& value,
            bool pretty = false)
        {
            auto result = pretty
                ? glz::write<detail::PrettyOptionsJsonWriteSettings>(value)
                : glz::write<detail::OptionsJsonWriteSettings>(value);

            if (!result)
            {
                return std::unexpected(detail::MakeSerializationError(
                    "Options serialization",
                    result.error()));
            }

            return std::move(*result);
        }

        static OptionsResult<void> DeserializeInto(
            T& value,
            std::string_view serialized)
        {
            const auto error =
                glz::read<detail::OptionsJsonReadSettings>(value, serialized);

            if (error)
            {
                return std::unexpected(detail::MakeSerializationError(
                    "Options deserialization",
                    error));
            }

            return {};
        }
    };

    /**
     * @brief Serialize a complete options value to deterministic JSON.
     */
    template<OptionsType T>
    OptionsResult<std::string> SerializeOptions(
        const T& value,
        bool pretty = false)
    {
        return OptionsSerializer<T>::Serialize(value, pretty);
    }

    /**
     * @brief Apply a partial JSON object to an existing options value.
     *
     * Fields absent from the serialized object retain their current values.
     */
    template<OptionsType T>
    OptionsResult<void> DeserializeOptionsInto(
        T& value,
        std::string_view serialized,
        std::string_view source = {})
    {
        auto result = OptionsSerializer<T>::DeserializeInto(value, serialized);
        if (!result && result.error().source.empty() && !source.empty())
        {
            result.error().source = source;
        }

        return result;
    }

    /**
     * @brief Deserialize a complete options value starting from T's defaults.
     */
    template<OptionsType T>
    OptionsResult<T> DeserializeOptions(
        std::string_view serialized,
        std::string_view source = {})
    {
        T value {};
        auto result = DeserializeOptionsInto(value, serialized, source);
        if (!result)
        {
            return std::unexpected(std::move(result.error()));
        }

        return value;
    }
}
