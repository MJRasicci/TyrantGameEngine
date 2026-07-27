#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "Internal/Desktop/IDesktopEventPump.hpp"

namespace TGE::Tests
{
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
            std::unique_lock lock(mutex);
            changed.wait_for(
                lock,
                maxWait,
                [this]
                {
                    return wakeRequested;
                });
            wakeRequested = false;
        }

        void Stop() noexcept override
        {
            {
                std::scoped_lock lock(mutex);
                stopped = true;
            }
            changed.notify_all();
        }

        [[nodiscard]] bool WaitUntilStarted(
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
        std::thread::id runner;
        bool wakeRequested { false };
        bool started { false };
        bool stopped { false };
    };
}
