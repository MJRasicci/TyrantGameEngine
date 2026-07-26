/**
 * @file JsonFileOptionsProvider.hpp
 * @brief JSON file source with optional polling-based live reload.
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "TGE/Export.hpp"
#include "TGE/Options/IOptionsProvider.hpp"

namespace TGE
{
    /**
     * @brief File source behavior.
     */
    struct JsonFileOptionsProviderSettings
    {
        bool optional { false };
        bool reloadOnChange { false };
        std::chrono::milliseconds pollingInterval {
            std::chrono::milliseconds(250)
        };
    };

    namespace detail
    {
        class OptionsFileWatchState;

        TGE_API std::shared_ptr<OptionsFileWatchState>
        CreateOptionsFileWatchState();

        TGE_API void RecordAppliedOptionsFile(
            const std::shared_ptr<OptionsFileWatchState>& state,
            bool exists,
            std::string_view contents);

        TGE_API OptionsSubscription WatchOptionsFile(
            std::filesystem::path path,
            std::chrono::milliseconds pollingInterval,
            std::function<bool()> callback,
            std::shared_ptr<OptionsFileWatchState> appliedState = {});
    }

    /**
     * @class JsonFileOptionsProvider
     * @brief Applies a partial JSON object from a filesystem path.
     */
    template<OptionsType TOptions>
    class JsonFileOptionsProvider final : public IOptionsProvider<TOptions>
    {
    public:
        explicit JsonFileOptionsProvider(
            std::filesystem::path pathValue,
            JsonFileOptionsProviderSettings settingsValue = {})
            : path(std::move(pathValue)),
              settings(settingsValue),
              name(std::format("JSON file {}", this->path.string())),
              watchState(detail::CreateOptionsFileWatchState())
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return name;
        }

        OptionsResult<void> Apply(TOptions& candidate) const override
        {
            std::error_code filesystemError;
            const bool exists = std::filesystem::exists(path, filesystemError);
            if (filesystemError)
            {
                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Io,
                    .source = name,
                    .message = filesystemError.message()
                });
            }

            if (!exists)
            {
                if (settings.optional)
                {
                    detail::RecordAppliedOptionsFile(
                        watchState,
                        false,
                        {});
                    return {};
                }

                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Io,
                    .source = name,
                    .message = "The required options file does not exist."
                });
            }

            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                // A file can disappear between exists() and open().
                if (settings.optional &&
                    !std::filesystem::exists(path, filesystemError) &&
                    !filesystemError)
                {
                    detail::RecordAppliedOptionsFile(
                        watchState,
                        false,
                        {});
                    return {};
                }

                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Io,
                    .source = name,
                    .message = "The options file could not be opened."
                });
            }

            std::string serialized(
                (std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char> {});

            if (stream.bad())
            {
                return std::unexpected(OptionsError {
                    .code = OptionsErrorCode::Io,
                    .source = name,
                    .message = "The options file could not be read completely."
                });
            }

            auto result =
                DeserializeOptionsInto(candidate, serialized, name);
            if (result)
            {
                detail::RecordAppliedOptionsFile(
                    watchState,
                    true,
                    serialized);
            }
            return result;
        }

        OptionsSubscription Watch(
            typename IOptionsProvider<TOptions>::ChangeCallback callback) override
        {
            if (!settings.reloadOnChange || !callback)
            {
                return {};
            }

            return detail::WatchOptionsFile(
                path,
                settings.pollingInterval,
                std::move(callback),
                watchState);
        }

    private:
        std::filesystem::path path;
        JsonFileOptionsProviderSettings settings;
        std::string name;
        std::shared_ptr<detail::OptionsFileWatchState> watchState;
    };
}
