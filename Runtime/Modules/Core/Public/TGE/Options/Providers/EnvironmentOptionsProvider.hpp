/**
 * @file EnvironmentOptionsProvider.hpp
 * @brief Process-environment source for typed options.
 */

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "TGE/Export.hpp"
#include "TGE/Options/IOptionsProvider.hpp"

namespace TGE
{
    /**
     * @brief Environment variable mapping behavior.
     */
    struct EnvironmentOptionsProviderSettings
    {
        std::string delimiter { "__" };
        bool lowercaseKeys { true };
    };

    namespace detail
    {
        TGE_API OptionsResult<std::string> ReadEnvironmentOptionsPatch(
            std::string_view prefix,
            std::string_view delimiter,
            bool lowercaseKeys);
    }

    /**
     * @class EnvironmentOptionsProvider
     * @brief Applies environment variables beneath a prefix as a JSON patch.
     *
     * With prefix TGE_EDITOR and delimiter "__",
     * TGE_EDITOR__rendering__vsync maps to rendering.vsync. Values that are
     * valid JSON literals retain their type; all others become JSON strings.
     */
    template<OptionsType TOptions>
    class EnvironmentOptionsProvider final : public IOptionsProvider<TOptions>
    {
    public:
        explicit EnvironmentOptionsProvider(
            std::string prefixValue,
            EnvironmentOptionsProviderSettings settingsValue = {})
            : prefix(std::move(prefixValue)),
              settings(std::move(settingsValue)),
              name("environment " + this->prefix)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return name;
        }

        OptionsResult<void> Apply(TOptions& candidate) const override
        {
            auto patch = detail::ReadEnvironmentOptionsPatch(
                prefix,
                settings.delimiter,
                settings.lowercaseKeys);
            if (!patch)
            {
                auto error = std::move(patch.error());
                if (error.source.empty())
                {
                    error.source = name;
                }
                return std::unexpected(std::move(error));
            }

            return DeserializeOptionsInto(candidate, *patch, name);
        }

    private:
        std::string prefix;
        EnvironmentOptionsProviderSettings settings;
        std::string name;
    };
}
