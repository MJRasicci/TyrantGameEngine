/**
 * @file IOptionsProvider.hpp
 * @brief Extensible typed source contract for runtime options.
 */

#pragma once

#include <functional>
#include <string_view>

#include "TGE/Options/OptionsError.hpp"
#include "TGE/Options/OptionsSerialization.hpp"
#include "TGE/Options/OptionsSubscription.hpp"

namespace TGE
{
    /**
     * @class IOptionsProvider
     * @brief Applies one source's partial configuration to an options candidate.
     *
     * Providers are applied in registration order to the same private candidate.
     * A provider may read a file, query the environment, call a database, or
     * assign values directly. If any provider fails, the candidate is discarded
     * and consumers retain the last-known-good snapshot.
     */
    template<OptionsType TOptions>
    class IOptionsProvider
    {
    public:
        /**
         * @brief Requests a reload and reports whether the source was accepted.
         */
        using ChangeCallback = std::function<bool()>;

        virtual ~IOptionsProvider() = default;

        /**
         * @brief Stable diagnostic name for this source.
         */
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

        /**
         * @brief Apply this source to a private candidate value.
         */
        virtual OptionsResult<void> Apply(TOptions& candidate) const = 0;

        /**
         * @brief Subscribe to external changes, if this provider can observe them.
         *
         * Providers without active monitoring return an empty token. The monitor
         * still supports explicit Reload(). Observable providers may retry a
         * notification when the callback returns false. Watch is installed
         * before the initial Apply; notifications raised during registration
         * are deferred and reloaded after that source is published.
         */
        virtual OptionsSubscription Watch(ChangeCallback)
        {
            return {};
        }
    };
}
