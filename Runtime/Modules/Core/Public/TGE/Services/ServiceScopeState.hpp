/**
 * @file ServiceScopeState.hpp
 * @brief Defines the lifecycle states of a dependency-injection scope.
 */

#pragma once

#include "TGE/Export.hpp"

namespace TGE
{
    /**
     * @brief Observable lifecycle state of a ServiceScope.
     */
    enum class TGE_API ServiceScopeState
    {
        /**
         * @brief The scope accepts service resolution, child scopes, and cleanup registrations.
         */
        Active,

        /**
         * @brief Ending has begun and no new scope work is accepted.
         */
        Ending,

        /**
         * @brief Children, cleanups, and scoped service instances have been released.
         */
        Ended
    };
}
