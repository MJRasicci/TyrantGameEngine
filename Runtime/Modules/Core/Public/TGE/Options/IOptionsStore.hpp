/**
 * @file IOptionsStore.hpp
 * @brief Explicit typed capability for persisting runtime options.
 */

#pragma once

#include "TGE/Options/OptionsConcepts.hpp"
#include "TGE/Options/OptionsError.hpp"

namespace TGE
{
    /**
     * @class IOptionsStore
     * @brief Persists a complete value to one explicitly selected source.
     *
     * Stores are intentionally separate from IOptionsProvider. Reading a
     * configuration source does not grant consumers permission to mutate it,
     * and sources such as the process environment should never expose this
     * capability.
     *
     * Save is synchronous and only persists the supplied value. It does not
     * directly publish an OptionsMonitor snapshot or alter provider precedence.
     */
    template<OptionsType TOptions>
    class IOptionsStore
    {
    public:
        virtual ~IOptionsStore() = default;

        /**
         * @brief Persist a complete options value to this store's fixed source.
         */
        [[nodiscard]] virtual OptionsResult<void> Save(
            const TOptions& value) = 0;
    };
}
