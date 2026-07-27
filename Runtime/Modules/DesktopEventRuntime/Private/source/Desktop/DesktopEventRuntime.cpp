#include "Internal/Desktop/DesktopEventRuntime.hpp"

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace TGE::Internal
{
    namespace
    {
        constexpr auto MaximumPumpWait =
            std::chrono::milliseconds(50);

        std::exception_ptr RuntimeStoppedException()
        {
            return std::make_exception_ptr(std::runtime_error(
                "The desktop event runtime is not accepting work."));
        }

        std::exception_ptr PumpStartException(
            const DesktopEventError& error)
        {
            return std::make_exception_ptr(std::runtime_error(
                error.message.empty()
                    ? "The desktop event pump failed to start."
                    : error.message));
        }
    }

    struct DesktopEventRuntime::State
    {
        enum class Phase
        {
            Ready,
            Starting,
            Running,
            Stopping,
            Stopped
        };

        explicit State(std::unique_ptr<IDesktopEventPump> pump)
            : pump(std::move(pump))
        {
        }

        std::unique_ptr<IDesktopEventPump> pump;
        mutable std::mutex mutex;
        std::deque<Command> commands;
        std::thread::id eventThread;
        Phase phase { Phase::Ready };
        bool accepting { true };
    };

    DesktopEventRuntime::DesktopEventRuntime(
        std::unique_ptr<IDesktopEventPump> pump)
    {
        if (!pump)
        {
            throw std::invalid_argument(
                "A desktop event runtime requires an event pump.");
        }
        state = std::make_shared<State>(std::move(pump));
    }

    DesktopEventRuntime::~DesktopEventRuntime()
    {
        Abandon(state);
    }

    bool DesktopEventRuntime::Post(
        std::function<void()> operation) noexcept
    {
        if (!operation)
        {
            return false;
        }

        bool wake = false;
        try
        {
            {
                std::scoped_lock lock(state->mutex);
                if (!state->accepting)
                {
                    return false;
                }

                state->commands.emplace_back(Command {
                    .execute = std::move(operation),
                    .reject = {}
                });

                wake = state->phase == State::Phase::Running;
            }

            if (wake)
            {
                state->pump->Wake();
            }

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    DesktopEventResult DesktopEventRuntime::Run(
        std::stop_token stopping)
    {
        const auto current = state;
        {
            std::scoped_lock lock(current->mutex);
            if (current->phase != State::Phase::Ready)
            {
                return std::unexpected(DesktopEventError {
                    .code = DesktopEventErrorCode::InvalidState,
                    .message =
                        "A desktop event runtime can only be driven once."
                });
            }

            current->phase = State::Phase::Starting;
            current->eventThread = std::this_thread::get_id();
        }

        std::stop_callback stopCallback(
            stopping,
            [current]() noexcept
            {
                RequestStop(current);
            });

        DesktopEventResult started;
        try
        {
            started = current->pump->Start();
        }
        catch (const std::exception& exception)
        {
            started = std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::PumpStartFailed,
                .message = exception.what()
            });
        }
        catch (...)
        {
            started = std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::PumpStartFailed,
                .message =
                    "The desktop event pump threw an unknown startup error."
            });
        }

        if (!started)
        {
            std::deque<Command> rejected;
            {
                std::scoped_lock lock(current->mutex);
                current->accepting = false;
                current->phase = State::Phase::Stopped;
                current->eventThread = {};
                rejected.swap(current->commands);
            }

            const auto failure = PumpStartException(started.error());
            for (auto& command : rejected)
            {
                if (command.reject)
                {
                    command.reject(failure);
                }
            }
            return started;
        }

        {
            std::scoped_lock lock(current->mutex);
            current->phase = State::Phase::Running;
        }

        for (;;)
        {
            std::deque<Command> pending;
            bool stopNow = false;
            {
                std::scoped_lock lock(current->mutex);
                pending.swap(current->commands);
                stopNow =
                    !current->accepting && pending.empty();
                if (stopNow)
                {
                    current->phase = State::Phase::Stopping;
                }
            }

            if (stopNow)
            {
                break;
            }

            for (auto& command : pending)
            {
                try
                {
                    command.execute();
                }
                catch (...)
                {
                    // Submit operations translate exceptions to their
                    // receiver. A throwing fire-and-forget Post is isolated
                    // so it cannot terminate the event loop.
                }
            }

            bool continuePumping = false;
            {
                std::scoped_lock lock(current->mutex);
                continuePumping =
                    current->accepting && current->commands.empty();
            }
            if (continuePumping)
            {
                current->pump->PumpEvents(MaximumPumpWait);
            }
        }

        current->pump->Stop();

        std::deque<Command> rejected;
        {
            std::scoped_lock lock(current->mutex);
            current->phase = State::Phase::Stopped;
            current->eventThread = {};
            rejected.swap(current->commands);
        }

        // Admission is closed before the drain begins, so this is only a
        // defensive completion path for an unexpected future state change.
        const auto stopped = RuntimeStoppedException();
        for (auto& command : rejected)
        {
            if (command.reject)
            {
                command.reject(stopped);
            }
        }

        return {};
    }

    void DesktopEventRuntime::RequestStop() noexcept
    {
        RequestStop(state);
    }

    bool DesktopEventRuntime::IsEventThread() const noexcept
    {
        std::scoped_lock lock(state->mutex);
        return state->eventThread == std::this_thread::get_id() &&
            state->phase != State::Phase::Ready &&
            state->phase != State::Phase::Stopped;
    }

    bool DesktopEventRuntime::IsAccepting() const noexcept
    {
        std::scoped_lock lock(state->mutex);
        return state->accepting;
    }

    void DesktopEventRuntime::Dispatch(
        const std::shared_ptr<State>& state,
        Command command) noexcept
    {
        bool executeInline = false;
        bool queued = false;
        bool wake = false;
        std::exception_ptr rejection;

        try
        {
            {
                std::scoped_lock lock(state->mutex);
                if (!state->accepting)
                {
                    rejection = RuntimeStoppedException();
                }
                else if (
                    state->eventThread == std::this_thread::get_id() &&
                    (state->phase == State::Phase::Starting ||
                     state->phase == State::Phase::Running))
                {
                    executeInline = true;
                }
                else
                {
                    state->commands.emplace_back(std::move(command));
                    queued = true;
                    wake = state->phase == State::Phase::Running;
                }
            }

            if (wake)
            {
                state->pump->Wake();
            }

            if (queued)
            {
                return;
            }
        }
        catch (...)
        {
            rejection = std::current_exception();
        }

        if (rejection)
        {
            command.reject(std::move(rejection));
        }
        else if (executeInline)
        {
            command.execute();
        }
    }

    void DesktopEventRuntime::RequestStop(
        const std::shared_ptr<State>& state) noexcept
    {
        bool wake = false;
        try
        {
            {
                std::scoped_lock lock(state->mutex);
                if (!state->accepting)
                {
                    return;
                }

                state->accepting = false;
                wake = state->phase == State::Phase::Running;
            }

            if (wake)
            {
                state->pump->Wake();
            }
        }
        catch (...)
        {
            // Stop requests form a noexcept process-shutdown boundary.
        }
    }

    void DesktopEventRuntime::Abandon(
        const std::shared_ptr<State>& state) noexcept
    {
        std::deque<Command> rejected;
        bool wake = false;
        try
        {
            {
                std::scoped_lock lock(state->mutex);
                state->accepting = false;

                if (state->phase == State::Phase::Running)
                {
                    wake = true;
                }
                else if (state->phase == State::Phase::Ready)
                {
                    state->phase = State::Phase::Stopped;
                    rejected.swap(state->commands);
                }
            }

            if (wake)
            {
                state->pump->Wake();
            }

            const auto stopped = RuntimeStoppedException();
            for (auto& command : rejected)
            {
                if (command.reject)
                {
                    command.reject(stopped);
                }
            }
        }
        catch (...)
        {
            // Destruction is best effort. Sender operation states retain State
            // until their rejection callback returns.
        }
    }
}
