#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "Internal/Desktop/IDesktopEventPump.hpp"

namespace TGE::Tests
{
    class FakeDesktopEventPump final
        : public Internal::IDesktopEventPump
    {
    public:
        [[nodiscard]] Internal::DesktopEventResult Start() override
        {
            std::scoped_lock lock(mutex);
            ++startCount;
            startThread = std::this_thread::get_id();
            started = true;
            changed.notify_all();
            return startResult;
        }

        void Wake() noexcept override
        {
            {
                std::scoped_lock lock(mutex);
                if (wakeCount == 0)
                {
                    firstWakeThread = std::this_thread::get_id();
                }
                lastWakeThread = std::this_thread::get_id();
                ++wakeCount;
                ++wakeGeneration;
            }
            changed.notify_all();
        }

        void PumpEvents(
            std::chrono::milliseconds maxWait) noexcept override
        {
            std::function<void()> event;
            std::unique_lock lock(mutex);
            ++pumpCount;
            lastPumpThread = std::this_thread::get_id();
            pumping = true;
            const auto observedWake = wakeGeneration;
            changed.notify_all();

            changed.wait_for(
                lock,
                maxWait,
                [this, observedWake]
                {
                    return wakeGeneration != observedWake ||
                           !events.empty();
                });

            if (!events.empty())
            {
                event = std::move(events.front());
                events.pop_front();
            }
            pumping = false;
            changed.notify_all();
            lock.unlock();

            if (event)
            {
                event();
            }
        }

        void QueueEvent(std::function<void()> event)
        {
            {
                std::scoped_lock lock(mutex);
                events.emplace_back(std::move(event));
            }
            changed.notify_all();
        }

        void Stop() noexcept override
        {
            {
                std::scoped_lock lock(mutex);
                ++stopCount;
                stopThread = std::this_thread::get_id();
                stopped = true;
            }
            changed.notify_all();
        }

        void SetStartResult(Internal::DesktopEventResult result)
        {
            std::scoped_lock lock(mutex);
            startResult = std::move(result);
        }

        [[nodiscard]] bool WaitUntilStarted(
            std::chrono::milliseconds timeout =
                std::chrono::seconds(2)) const
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

        [[nodiscard]] bool WaitUntilPumping(
            std::chrono::milliseconds timeout =
                std::chrono::seconds(2)) const
        {
            std::unique_lock lock(mutex);
            return changed.wait_for(
                lock,
                timeout,
                [this]
                {
                    return pumping;
                });
        }

        [[nodiscard]] bool WaitUntilStopped(
            std::chrono::milliseconds timeout =
                std::chrono::seconds(2)) const
        {
            std::unique_lock lock(mutex);
            return changed.wait_for(
                lock,
                timeout,
                [this]
                {
                    return stopped;
                });
        }

        [[nodiscard]] std::size_t StartCount() const
        {
            std::scoped_lock lock(mutex);
            return startCount;
        }

        [[nodiscard]] std::size_t PumpCount() const
        {
            std::scoped_lock lock(mutex);
            return pumpCount;
        }

        [[nodiscard]] std::size_t StopCount() const
        {
            std::scoped_lock lock(mutex);
            return stopCount;
        }

        [[nodiscard]] std::size_t WakeCount() const
        {
            std::scoped_lock lock(mutex);
            return wakeCount;
        }

        [[nodiscard]] std::thread::id StartThread() const
        {
            std::scoped_lock lock(mutex);
            return startThread;
        }

        [[nodiscard]] std::thread::id LastPumpThread() const
        {
            std::scoped_lock lock(mutex);
            return lastPumpThread;
        }

        [[nodiscard]] std::thread::id StopThread() const
        {
            std::scoped_lock lock(mutex);
            return stopThread;
        }

        [[nodiscard]] std::thread::id FirstWakeThread() const
        {
            std::scoped_lock lock(mutex);
            return firstWakeThread;
        }

        [[nodiscard]] std::thread::id LastWakeThread() const
        {
            std::scoped_lock lock(mutex);
            return lastWakeThread;
        }

    private:
        mutable std::mutex mutex;
        mutable std::condition_variable changed;
        Internal::DesktopEventResult startResult;
        std::size_t startCount { 0 };
        std::size_t pumpCount { 0 };
        std::size_t stopCount { 0 };
        std::size_t wakeCount { 0 };
        std::size_t wakeGeneration { 0 };
        std::deque<std::function<void()>> events;
        std::thread::id startThread;
        std::thread::id lastPumpThread;
        std::thread::id stopThread;
        std::thread::id firstWakeThread;
        std::thread::id lastWakeThread;
        bool started { false };
        bool pumping { false };
        bool stopped { false };
    };
}
