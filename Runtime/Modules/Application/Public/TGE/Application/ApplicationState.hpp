/**
 * @file ApplicationState.hpp
 * @brief States in the TGE application lifecycle.
 */

#pragma once

namespace TGE
{
    /**
     * @brief Observable state of an Application instance.
     */
    enum class ApplicationState
    {
        /** @brief Service registration remains mutable; execution has not begun. */
        Created,

        /** @brief The provider is being built and hosted services are starting. */
        Starting,

        /** @brief Startup completed and the application is waiting for shutdown. */
        Running,

        /** @brief Successfully started hosted services are stopping. */
        Stopping,

        /** @brief The lifecycle completed successfully and cannot be restarted. */
        Stopped,

        /** @brief Startup or shutdown failed after cleanup was attempted. */
        Failed
    };
}
