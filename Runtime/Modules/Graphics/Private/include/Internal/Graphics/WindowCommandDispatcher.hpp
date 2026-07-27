#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "TGE/Execution/Task.hpp"

#if TGE_HAS_NATIVE_STD_EXECUTION
namespace TGE::Internal
{
    namespace WindowExecutionBackend = std::execution;
}
#else
namespace TGE::Internal
{
    namespace WindowExecutionBackend = stdexec;
}
#endif

namespace TGE::Internal
{
    class IWindowPlatform;
    class IWindowPlatformEventSink;

    /**
     * @brief Owns the serialized platform thread and its event pump.
     */
    class WindowCommandDispatcher final
    {
    public:
        WindowCommandDispatcher(
            std::unique_ptr<IWindowPlatform> platform,
            std::shared_ptr<IWindowPlatformEventSink> sink);
        ~WindowCommandDispatcher();

        WindowCommandDispatcher(const WindowCommandDispatcher&) = delete;
        WindowCommandDispatcher& operator=(const WindowCommandDispatcher&) =
            delete;

        void Stop() noexcept;
        [[nodiscard]] bool IsDispatcherThread() const noexcept;

        /**
         * @brief Queue work for a later dispatcher turn, even on its thread.
         *
         * Event sinks use this to coalesce high-frequency platform updates
         * delivered during one event-pump pass.
         */
        bool Post(std::function<void()> operation) noexcept;

        template<class TResult>
        class Sender
        {
        public:
            using sender_concept = WindowExecutionBackend::sender_tag;
            using completion_signatures =
                WindowExecutionBackend::completion_signatures<
                    WindowExecutionBackend::set_value_t(TResult),
                    WindowExecutionBackend::set_error_t(std::exception_ptr)>;

            Sender(
                WindowCommandDispatcher& dispatcher,
                std::function<TResult()> operation)
                : dispatcher(&dispatcher),
                  operation(std::move(operation))
            {
            }

            template<class TReceiver>
            struct Operation
            {
                using operation_state_concept =
                    WindowExecutionBackend::operation_state_tag;

                Operation(
                    WindowCommandDispatcher& dispatcher,
                    std::function<TResult()> function,
                    TReceiver receiver)
                    : dispatcher(&dispatcher),
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
                    auto execute = [this]() noexcept
                    {
                        try
                        {
                            auto result = function();
                            WindowExecutionBackend::set_value(
                                std::move(receiver),
                                std::move(result));
                        }
                        catch (...)
                        {
                            WindowExecutionBackend::set_error(
                                std::move(receiver),
                                std::current_exception());
                        }
                    };

                    if (!dispatcher->Dispatch(std::move(execute)))
                    {
                        WindowExecutionBackend::set_error(
                            std::move(receiver),
                            std::make_exception_ptr(std::runtime_error(
                                "Window dispatcher is stopped.")));
                    }
                }

                WindowCommandDispatcher* dispatcher;
                std::function<TResult()> function;
                TReceiver receiver;
            };

            template<class TReceiver>
            auto connect(TReceiver receiver) const
            {
                return Operation<TReceiver> {
                    *dispatcher,
                    operation,
                    std::move(receiver)
                };
            }

        private:
            WindowCommandDispatcher* dispatcher;
            std::function<TResult()> operation;
        };

        template<class TFunction>
        [[nodiscard]] auto Submit(TFunction&& function)
        {
            using TResult = std::invoke_result_t<std::decay_t<TFunction>&>;
            return Sender<TResult> {
                *this,
                std::function<TResult()> {
                    std::forward<TFunction>(function)
                }
            };
        }

        [[nodiscard]] IWindowPlatform& Platform() noexcept;
        [[nodiscard]] bool IsAccepting() const noexcept;

    private:
        bool Dispatch(std::function<void()> operation) noexcept;

        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
