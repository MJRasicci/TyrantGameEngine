/**
 * @file OptionsBuilder.hpp
 * @brief Fluent composition API for typed options.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "TGE/Options/OptionsMonitor.hpp"
#include "TGE/Options/Providers/EnvironmentOptionsProvider.hpp"
#include "TGE/Options/Providers/JsonFileOptionsProvider.hpp"

namespace TGE
{
    /**
     * @class OptionsBuilder
     * @brief Adds ordered sources, configuration steps, and validators.
     *
     * Every fluent operation immediately rebuilds the private candidate so
     * startup configuration errors surface at the registration site.
     */
    template<OptionsType TOptions>
    class OptionsBuilder final
    {
    public:
        explicit OptionsBuilder(
            std::shared_ptr<OptionsMonitor<TOptions>> monitorValue)
            : monitor(std::move(monitorValue))
        {
            if (!this->monitor)
            {
                throw std::invalid_argument(
                    "OptionsBuilder requires a monitor.");
            }
        }

        OptionsBuilder(const OptionsBuilder&) = delete;
        OptionsBuilder& operator=(const OptionsBuilder&) = delete;
        OptionsBuilder(OptionsBuilder&&) noexcept = default;
        OptionsBuilder& operator=(OptionsBuilder&&) noexcept = default;

        /**
         * @brief Add an arbitrary typed provider.
         */
        OptionsBuilder& AddProvider(
            std::shared_ptr<IOptionsProvider<TOptions>> provider)
        {
            Require(monitor->AddProvider(std::move(provider)));
            return *this;
        }

        /**
         * @brief Apply a partial JSON file in the current source order.
         */
        OptionsBuilder& FromJsonFile(
            std::filesystem::path path,
            JsonFileOptionsProviderSettings settings = {})
        {
            return AddProvider(
                std::make_shared<JsonFileOptionsProvider<TOptions>>(
                    std::move(path),
                    settings));
        }

        /**
         * @brief Apply process environment variables beneath a prefix.
         */
        OptionsBuilder& FromEnvironment(
            std::string prefix,
            EnvironmentOptionsProviderSettings settings = {})
        {
            return AddProvider(
                std::make_shared<EnvironmentOptionsProvider<TOptions>>(
                    std::move(prefix),
                    std::move(settings)));
        }

        /**
         * @brief Add an ordered programmatic configuration step.
         */
        template<class TConfigure>
            requires std::invocable<TConfigure&, TOptions&> &&
                (std::same_as<
                     std::invoke_result_t<TConfigure&, TOptions&>,
                     void> ||
                 std::same_as<
                     std::invoke_result_t<TConfigure&, TOptions&>,
                     OptionsResult<void>>)
        OptionsBuilder& Configure(
            TConfigure&& configure,
            std::string source = "programmatic configuration")
        {
            Require(monitor->AddConfigure(
                [configure = std::forward<TConfigure>(configure)](
                    TOptions& options) mutable -> OptionsResult<void>
                {
                    if constexpr (std::same_as<
                                      std::invoke_result_t<
                                          TConfigure&,
                                          TOptions&>,
                                      void>)
                    {
                        std::invoke(configure, options);
                        return {};
                    }
                    else
                    {
                        return std::invoke(configure, options);
                    }
                },
                std::move(source)));
            return *this;
        }

        /**
         * @brief Validate every candidate before it can be published.
         */
        template<class TPredicate>
            requires std::predicate<TPredicate&, const TOptions&>
        OptionsBuilder& Validate(
            TPredicate&& predicate,
            std::string message)
        {
            Require(monitor->AddValidator(
                std::move_only_function<bool(const TOptions&)>(
                    std::forward<TPredicate>(predicate)),
                std::move(message)));
            return *this;
        }

        /**
         * @brief Concrete authority for explicit reloads and runtime updates.
         */
        [[nodiscard]] std::shared_ptr<OptionsMonitor<TOptions>>
        GetMonitor() const noexcept
        {
            return monitor;
        }

    private:
        static void Require(OptionsResult<std::uint64_t> result)
        {
            if (!result)
            {
                throw OptionsException(std::move(result.error()));
            }
        }

        std::shared_ptr<OptionsMonitor<TOptions>> monitor;
    };
}
