#pragma once

#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>

#include "Internal/Desktop/IDesktopEventPump.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"

#if TGE_HAS_NATIVE_STD_EXECUTION
namespace TGE::Internal
{
    namespace DesktopExecutionBackend = std::execution;
}
#else
namespace TGE::Internal
{
    namespace DesktopExecutionBackend = stdexec;
}
#endif

namespace TGE::Internal
{
    namespace detail
    {
        template<class TResult>
        struct DesktopCompletionSignatures
        {
            using Type =
                DesktopExecutionBackend::completion_signatures<
                    DesktopExecutionBackend::set_value_t(TResult),
                    DesktopExecutionBackend::set_error_t(
                        std::exception_ptr)>;
        };

        template<>
        struct DesktopCompletionSignatures<void>
        {
            using Type =
                DesktopExecutionBackend::completion_signatures<
                    DesktopExecutionBackend::set_value_t(),
                    DesktopExecutionBackend::set_error_t(
                        std::exception_ptr)>;
        };
    }

    /**
     * @brief Thread-safe command facade around one externally driven event pump.
     *
     * Run owns no thread. A GUI composition or test host chooses the event
     * thread and drives Run exactly once. Submit executes inline when called
     * from that thread, which permits synchronous facade re-entry from event
     * callbacks. Post always queues work for a later event-loop turn.
     * Operations may be admitted before Run starts; their senders complete
     * only after the caller-owned runner begins draining the queue.
     *
     * RequestStop closes command admission immediately. Work accepted before
     * that transition is drained before the pump is stopped.
     */
    class TGE_API DesktopEventRuntime final
    {
    private:
        struct State;

        struct Command
        {
            std::function<void()> execute;
            std::function<void(std::exception_ptr)> reject;
        };

    public:
        explicit DesktopEventRuntime(
            std::unique_ptr<IDesktopEventPump> pump);
        ~DesktopEventRuntime();

        DesktopEventRuntime(const DesktopEventRuntime&) = delete;
        DesktopEventRuntime& operator=(const DesktopEventRuntime&) = delete;
        DesktopEventRuntime(DesktopEventRuntime&&) = delete;
        DesktopEventRuntime& operator=(DesktopEventRuntime&&) = delete;

        template<class TResult>
        class Sender
        {
        public:
            using sender_concept =
                DesktopExecutionBackend::sender_tag;
            using completion_signatures =
                typename detail::DesktopCompletionSignatures<
                    TResult>::Type;

            Sender(
                std::shared_ptr<State> state,
                std::function<TResult()> operation)
                : state(std::move(state)),
                  operation(std::move(operation))
            {
            }

            template<class TReceiver>
            struct Operation
            {
                using operation_state_concept =
                    DesktopExecutionBackend::operation_state_tag;

                Operation(
                    std::shared_ptr<State> state,
                    std::function<TResult()> function,
                    TReceiver receiver)
                    : state(std::move(state)),
                      function(std::move(function)),
                      receiver(std::move(receiver))
                {
                }

                Operation(const Operation&) = delete;
                Operation& operator=(const Operation&) = delete;
                Operation(Operation&&) = delete;
                Operation& operator=(Operation&&) = delete;

                void start() noexcept
                {
                    Command command {
                        .execute =
                            [this]() noexcept
                            {
                                try
                                {
                                    if constexpr (
                                        std::is_void_v<TResult>)
                                    {
                                        function();
                                        DesktopExecutionBackend::set_value(
                                            std::move(receiver));
                                    }
                                    else
                                    {
                                        auto result = function();
                                        DesktopExecutionBackend::set_value(
                                            std::move(receiver),
                                            std::move(result));
                                    }
                                }
                                catch (...)
                                {
                                    DesktopExecutionBackend::set_error(
                                        std::move(receiver),
                                        std::current_exception());
                                }
                            },
                        .reject =
                            [this](std::exception_ptr error) noexcept
                            {
                                DesktopExecutionBackend::set_error(
                                    std::move(receiver),
                                    std::move(error));
                            }
                    };

                    DesktopEventRuntime::Dispatch(
                        state,
                        std::move(command));
                }

                std::shared_ptr<State> state;
                std::function<TResult()> function;
                TReceiver receiver;
            };

            template<class TReceiver>
            auto connect(TReceiver receiver) const
            {
                return Operation<TReceiver> {
                    state,
                    operation,
                    std::move(receiver)
                };
            }

        private:
            std::shared_ptr<State> state;
            std::function<TResult()> operation;
        };

        template<class TFunction>
        [[nodiscard]] auto Submit(TFunction&& function)
        {
            using TResult =
                std::invoke_result_t<std::decay_t<TFunction>&>;
            return Sender<TResult> {
                state,
                std::function<TResult()> {
                    std::forward<TFunction>(function)
                }
            };
        }

        /**
         * @brief Queue fire-and-forget work for the next event-loop turn.
         *
         * Unlike Submit, Post never invokes the operation inline, including
         * when called from the event thread. Returns false for an empty
         * operation or after command admission has closed.
         */
        bool Post(std::function<void()> operation) noexcept;

        /**
         * @brief Drive the pump and accepted commands on the calling thread.
         *
         * A runtime is single-run. The call returns after stop is requested and
         * every command accepted before admission closed has completed.
         * Repeated and concurrent calls return InvalidState without touching
         * the pump.
         */
        [[nodiscard]] DesktopEventResult Run(
            std::stop_token stopping = {});

        void RequestStop() noexcept;

        [[nodiscard]] bool IsEventThread() const noexcept;
        [[nodiscard]] bool IsAccepting() const noexcept;

    private:
        static void Dispatch(
            const std::shared_ptr<State>& state,
            Command command) noexcept;
        static void RequestStop(
            const std::shared_ptr<State>& state) noexcept;
        static void Abandon(
            const std::shared_ptr<State>& state) noexcept;

        std::shared_ptr<State> state;
    };
}
