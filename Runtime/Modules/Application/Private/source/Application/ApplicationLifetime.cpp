#include "TGE/Application/ApplicationLifetime.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <utility>

#if TGE_HAS_NATIVE_STD_EXECUTION
namespace TGEExecutionBackend = std::execution;
#else
namespace TGEExecutionBackend = stdexec;
#endif

namespace TGE
{
    struct ApplicationLifetime::State
    {
        struct StopSender
        {
            using sender_concept = TGEExecutionBackend::sender_tag;
            using completion_signatures =
                TGEExecutionBackend::completion_signatures<
                    TGEExecutionBackend::set_value_t()>;

            std::shared_ptr<State> state;
            void* context;
            WaitStartedCallback waitStarted;

            template<class TReceiver>
            struct Operation
            {
                using operation_state_concept =
                    TGEExecutionBackend::operation_state_tag;

                Operation(
                    std::shared_ptr<State> state,
                    void* context,
                    WaitStartedCallback waitStarted,
                    TReceiver receiver)
                    : state(std::move(state)),
                      context(context),
                      waitStarted(waitStarted),
                      receiver(std::move(receiver))
                {
                }

                Operation(const Operation&) = delete;
                Operation& operator=(const Operation&) = delete;
                Operation(Operation&&) = delete;
                Operation& operator=(Operation&&) = delete;

                ~Operation()
                {
                    std::scoped_lock lock(state->mutex);
                    if (state->waiter == this)
                    {
                        state->waiter = nullptr;
                        state->completeWaiter = nullptr;
                    }
                }

                void start() noexcept
                {
                    bool completeImmediately = false;

                    {
                        std::scoped_lock lock(state->mutex);

                        if (state->stopRequested.load())
                        {
                            completeImmediately = true;
                        }
                        else if (state->waiter)
                        {
                            std::terminate();
                        }
                        else
                        {
                            state->waiter = this;
                            state->completeWaiter = &Operation::Complete;
                            waitStarted(context);
                        }
                    }

                    if (completeImmediately)
                    {
                        Complete(this);
                    }
                }

                static void Complete(void* waiter) noexcept
                {
                    auto* operation = static_cast<Operation*>(waiter);
                    TGEExecutionBackend::set_value(
                        std::move(operation->receiver));
                }

                std::shared_ptr<State> state;
                void* context;
                WaitStartedCallback waitStarted;
                TReceiver receiver;
            };

            template<class TReceiver>
            auto connect(TReceiver receiver) const
            {
                return Operation<TReceiver> {
                    state,
                    context,
                    waitStarted,
                    std::move(receiver)
                };
            }
        };

        std::stop_source stopSource;
        std::atomic<bool> stopRequested { false };
        std::atomic<int> exitCode { 0 };
        std::mutex mutex;
        void* waiter = nullptr;
        void (*completeWaiter)(void*) noexcept = nullptr;
    };

    ApplicationLifetime::ApplicationLifetime()
        : state(std::make_shared<State>())
    {
    }

    ApplicationLifetime::~ApplicationLifetime() = default;

    std::stop_token ApplicationLifetime::GetStoppingToken() const noexcept
    {
        return state->stopSource.get_token();
    }

    bool ApplicationLifetime::IsStopRequested() const noexcept
    {
        return state->stopRequested.load();
    }

    bool ApplicationLifetime::RequestStop(int exitCode) noexcept
    {
        void* waiter = nullptr;
        void (*completeWaiter)(void*) noexcept = nullptr;

        {
            std::scoped_lock lock(state->mutex);

            if (state->stopRequested.load())
            {
                return false;
            }

            state->exitCode.store(exitCode);
            state->stopRequested.store(true);
            waiter = std::exchange(state->waiter, nullptr);
            completeWaiter =
                std::exchange(state->completeWaiter, nullptr);
        }

        state->stopSource.request_stop();

        if (completeWaiter)
        {
            completeWaiter(waiter);
        }

        return true;
    }

    int ApplicationLifetime::GetExitCode() const noexcept
    {
        return state->exitCode.load();
    }

    Task<void> ApplicationLifetime::WaitForStopAsync(
        void* context,
        WaitStartedCallback waitStarted)
    {
        co_await State::StopSender {
            state,
            context,
            waitStarted
        };
    }
}
