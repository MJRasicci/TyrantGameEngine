/**
 * @file Task.hpp
 * @brief C++26 coroutine task and synchronous-wait compatibility surface.
 */

#pragma once

#include <utility>

#include "TGE/Features.hpp"

#if TGE_HAS_NATIVE_STD_EXECUTION
    #include <execution>
#else
    #include <stdexec/execution.hpp>
#endif

namespace TGE::Execution
{
    /**
     * @brief Lazy coroutine task used by asynchronous TGE APIs.
     * @tparam T Successful completion value, or void.
     *
     * The alias follows std::execution::task. Until the active standard
     * library implements that C++26 facility, TGE supplies the same model
     * through its pinned stdexec compatibility backend.
     */
    template<class T = void>
#if TGE_HAS_NATIVE_STD_EXECUTION
    using Task = std::execution::task<T>;
#else
    using Task = stdexec::task<T>;
#endif

    /**
     * @brief Block the current thread until a sender completes.
     *
     * This function is intended for process-level synchronous entrypoints.
     * Asynchronous code should compose or await the sender instead.
     */
    template<class TSender>
    decltype(auto) SyncWait(TSender&& sender)
    {
#if TGE_HAS_NATIVE_STD_EXECUTION
        return std::this_thread::sync_wait(std::forward<TSender>(sender));
#else
        return stdexec::sync_wait(std::forward<TSender>(sender));
#endif
    }

    /**
     * @brief Convert sender cancellation into an error completion.
     *
     * Coroutine tasks propagate a stopped completion directly to their parent.
     * Lifecycle and transactional code can use this adapter when cancellation
     * must participate in ordinary error handling and cleanup.
     */
    template<class TSender, class TError>
    auto StoppedAsError(TSender&& sender, TError&& error)
    {
#if TGE_HAS_NATIVE_STD_EXECUTION
        return std::execution::stopped_as_error(
            std::forward<TSender>(sender),
            std::forward<TError>(error));
#else
        return stdexec::stopped_as_error(
            std::forward<TSender>(sender),
            std::forward<TError>(error));
#endif
    }
}

namespace TGE
{
    /**
     * @brief Convenience alias for the engine's C++26 execution task.
     */
    template<class T = void>
    using Task = Execution::Task<T>;
}
