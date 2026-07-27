#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "Internal/Desktop/IDesktopEventPump.hpp"

namespace TGE::Tests
{
    /**
     * @brief Deterministic blocking pump shared by fake desktop adapters.
     */
    class FakeDesktopEventPump final
        : public Internal::IDesktopEventPump
    {
    public:
        Internal::DesktopEventResult Start() override
        {
            {
                std::scoped_lock lock(mutex);
                started = true;
                runner = std::this_thread::get_id();
            }
            changed.notify_all();
            return {};
        }

        void Wake() noexcept override
        {
            {
                std::scoped_lock lock(mutex);
                wakeRequested = true;
            }
            changed.notify_all();
        }

        void PumpEvents(
            std::chrono::milliseconds maxWait) noexcept override
        {
            std::function<void()> event;
            {
                std::unique_lock lock(mutex);
                changed.wait_for(
                    lock,
                    maxWait,
                    [this]
                    {
                        return wakeRequested || !events.empty();
                    });
                wakeRequested = false;
                if (!events.empty())
                {
                    event = std::move(events.front());
                    events.pop_front();
                }
            }

            if (event)
            {
                try
                {
                    event();
                }
                catch (...)
                {
                    // Native event pumps do not allow one translated event to
                    // terminate the process-level loop.
                }
            }
        }

        void Stop() noexcept override
        {
            {
                std::scoped_lock lock(mutex);
                stopped = true;
            }
            changed.notify_all();
        }

        void Queue(std::function<void()> event)
        {
            {
                std::scoped_lock lock(mutex);
                events.emplace_back(std::move(event));
                wakeRequested = true;
            }
            changed.notify_all();
        }

        bool WaitUntilStarted(
            std::chrono::milliseconds timeout =
                std::chrono::seconds(2))
        {
            std::unique_lock lock(mutex);
            return changed.wait_for(
                lock,
                timeout,
                [this]
                {
                    return started;
                });
        }

        [[nodiscard]] std::thread::id RunnerThread() const
        {
            std::scoped_lock lock(mutex);
            return runner;
        }

    private:
        mutable std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::function<void()>> events;
        std::thread::id runner;
        bool wakeRequested { false };
        bool started { false };
        bool stopped { false };
    };
}
