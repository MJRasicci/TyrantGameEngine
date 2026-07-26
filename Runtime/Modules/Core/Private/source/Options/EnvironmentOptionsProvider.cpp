#include "TGE/Options/Providers/EnvironmentOptionsProvider.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glaze/json/generic.hpp>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#elif defined(__APPLE__)
    #include <crt_externs.h>
#else
extern char** environ;
#endif

namespace TGE::detail
{
    namespace
    {
        using EnvironmentEntry = std::pair<std::string, std::string>;

        char LowerAscii(char value) noexcept
        {
            return static_cast<char>(std::tolower(
                static_cast<unsigned char>(value)));
        }

        std::string Lowercase(std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), LowerAscii);
            return result;
        }

#if defined(_WIN32)
        OptionsResult<std::string> ToUtf8(std::wstring_view value)
        {
            if (value.empty())
            {
                return std::string {};
            }

            const int length = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (length <= 0)
            {
                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Io,
                    .source = "process environment",
                    .message =
                        "An environment entry could not be converted to UTF-8."
                });
            }

            std::string result(static_cast<std::size_t>(length), '\0');
            const int converted = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                length,
                nullptr,
                nullptr);
            if (converted != length)
            {
                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Io,
                    .source = "process environment",
                    .message =
                        "An environment entry could not be converted to UTF-8."
                });
            }

            return result;
        }

        struct EnvironmentBlockDeleter
        {
            void operator()(wchar_t* block) const noexcept
            {
                if (block)
                {
                    FreeEnvironmentStringsW(block);
                }
            }
        };
#endif

        OptionsResult<std::vector<EnvironmentEntry>> ReadEnvironment()
        {
            std::vector<EnvironmentEntry> result;

#if defined(_WIN32)
            std::unique_ptr<wchar_t, EnvironmentBlockDeleter> block(
                GetEnvironmentStringsW());
            if (!block)
            {
                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Io,
                    .source = "process environment",
                    .message = "Windows did not provide an environment block."
                });
            }

            for (const wchar_t* entry = block.get(); *entry != L'\0';)
            {
                const std::wstring_view item(entry);
                entry += item.size() + 1;

                const auto separator = item.find(L'=');
                if (separator == std::wstring_view::npos || separator == 0)
                {
                    continue;
                }

                auto key = ToUtf8(item.substr(0, separator));
                if (!key)
                {
                    return std::unexpected(std::move(key.error()));
                }

                auto value = ToUtf8(item.substr(separator + 1));
                if (!value)
                {
                    return std::unexpected(std::move(value.error()));
                }

                result.emplace_back(
                    std::move(*key),
                    std::move(*value));
            }
#elif defined(__APPLE__)
            for (char** entry = *_NSGetEnviron();
                 entry && *entry;
                 ++entry)
            {
                const std::string_view item(*entry);
                const auto separator = item.find('=');
                if (separator == std::string_view::npos || separator == 0)
                {
                    continue;
                }

                result.emplace_back(
                    item.substr(0, separator),
                    item.substr(separator + 1));
            }
#else
            for (char** entry = environ; entry && *entry; ++entry)
            {
                const std::string_view item(*entry);
                const auto separator = item.find('=');
                if (separator == std::string_view::npos || separator == 0)
                {
                    continue;
                }

                result.emplace_back(
                    item.substr(0, separator),
                    item.substr(separator + 1));
            }
#endif

            return result;
        }

        bool StartsWith(
            std::string_view value,
            std::string_view prefix) noexcept
        {
#if defined(_WIN32)
            if (value.size() < prefix.size())
            {
                return false;
            }

            for (std::size_t index = 0; index < prefix.size(); ++index)
            {
                if (LowerAscii(value[index]) != LowerAscii(prefix[index]))
                {
                    return false;
                }
            }
            return true;
#else
            return value.starts_with(prefix);
#endif
        }

        std::vector<std::string> SplitPath(
            std::string_view value,
            std::string_view delimiter,
            bool lowercaseKeys)
        {
            std::vector<std::string> result;
            std::size_t offset = 0;

            while (offset <= value.size())
            {
                const auto next = value.find(delimiter, offset);
                const auto segment = value.substr(
                    offset,
                    next == std::string_view::npos
                        ? value.size() - offset
                        : next - offset);
                if (segment.empty())
                {
                    return {};
                }

                result.emplace_back(
                    lowercaseKeys ? Lowercase(segment) : std::string(segment));

                if (next == std::string_view::npos)
                {
                    break;
                }
                offset = next + delimiter.size();
            }

            return result;
        }
    }

    OptionsResult<std::string> ReadEnvironmentOptionsPatch(
        std::string_view prefix,
        std::string_view delimiter,
        bool lowercaseKeys)
    {
        if (prefix.empty())
        {
            return std::unexpected(OptionsError {
                .code = OptionsErrorCode::Provider,
                .source = "process environment",
                .message = "An environment options prefix cannot be empty."
            });
        }

        if (delimiter.empty())
        {
            return std::unexpected(OptionsError {
                .code = OptionsErrorCode::Provider,
                .source = "process environment",
                .message = "An environment options delimiter cannot be empty."
            });
        }

        auto entries = ReadEnvironment();
        if (!entries)
        {
            return std::unexpected(std::move(entries.error()));
        }

        std::string qualifiedPrefix(prefix);
        if (!qualifiedPrefix.ends_with(delimiter))
        {
            qualifiedPrefix += delimiter;
        }

        std::ranges::sort(*entries, {}, &EnvironmentEntry::first);

        glz::generic_u64 document;
        document.data = glz::generic_u64::object_t {};

        for (const auto& [key, rawValue] : *entries)
        {
            if (!StartsWith(key, qualifiedPrefix))
            {
                continue;
            }

            const auto pathText =
                std::string_view(key).substr(qualifiedPrefix.size());
            auto path = SplitPath(pathText, delimiter, lowercaseKeys);
            if (path.empty())
            {
                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Provider,
                    .source = "process environment",
                    .message = std::format(
                        "Environment variable {} has an invalid options path.",
                        key)
                });
            }

            glz::generic_u64* node = &document;
            for (std::size_t index = 0; index + 1 < path.size(); ++index)
            {
                if (!node->is_object())
                {
                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Provider,
                        .source = "process environment",
                        .message = std::format(
                            "Environment variable {} conflicts with another options path.",
                            key)
                    });
                }

                auto& object = node->get_object();
                const auto existing = object.find(path[index]);
                if (existing == object.end())
                {
                    node = &(*node)[path[index]];
                    node->data = glz::generic_u64::object_t {};
                }
                else
                {
                    node = &existing->second;
                    if (!node->is_object())
                    {
                        return std::unexpected(OptionsError {
                            .code = OptionsErrorCode::Provider,
                            .source = "process environment",
                            .message = std::format(
                                "Environment variable {} conflicts with another options path.",
                                key)
                        });
                    }
                }
            }

            auto& object = node->get_object();
            if (object.find(path.back()) != object.end())
            {
                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Provider,
                    .source = "process environment",
                    .message = std::format(
                        "Environment variable {} conflicts with another options path.",
                        key)
                });
            }

            auto parsed = glz::read_json<glz::generic_u64>(rawValue);
            if (parsed)
            {
                (*node)[path.back()] = std::move(*parsed);
            }
            else
            {
                (*node)[path.back()] = rawValue;
            }
        }

        auto serialized = glz::write_json(document);
        if (!serialized)
        {
            return std::unexpected(OptionsError {
                .code = OptionsErrorCode::Serialization,
                .source = "process environment",
                .message = glz::format_error(serialized.error())
            });
        }

        return std::move(*serialized);
    }
}
