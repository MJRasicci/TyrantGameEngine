#include "Internal/Graphics/WindowCommandDispatcher.hpp"

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include "Internal/Graphics/IWindowPlatform.hpp"

namespace TGE::Internal
{
    struct WindowCommandDispatcher::Impl
    {
        struct WorkerState
        {
            WorkerState(
                std::unique_ptr<IWindowPlatform> platform,
                std::shared_ptr<IWindowPlatformEventSink> sink)
                : platform(std::move(platform)),
                  sink(std::move(sink))
            {
                this->platform->SetEventSink(this->sink.get());
            }

            ~WorkerState()
            {
                platform->SetEventSink(nullptr);
            }

            void Run() noexcept
            {
                currentWorker = this;

                for (;;)
                {
                    std::deque<std::function<void()>> pending;
                    {
                        std::scoped_lock lock(mutex);
                        pending.swap(commands);
                    }

                    for (auto& command : pending)
                    {
                        command();
                    }

                    {
                        std::scoped_lock lock(mutex);
                        if (stopping && commands.empty())
                        {
                            break;
                        }
                    }

                    platform->PumpEvents(std::chrono::milliseconds(50));
                }

                currentWorker = nullptr;
                platform->SetEventSink(nullptr);
                sink.reset();
            }

            bool Dispatch(std::function<void()> operation) noexcept
            {
                if (currentWorker == this)
                {
                    operation();
                    return true;
                }

                {
                    std::scoped_lock lock(mutex);
                    if (!accepting)
                    {
                        return false;
                    }
                    commands.emplace_back(std::move(operation));
                }

                platform->WakeEventLoop();
                return true;
            }

            bool Post(std::function<void()> operation) noexcept
            {
                {
                    std::scoped_lock lock(mutex);
                    if (!accepting)
                    {
                        return false;
                    }
                    commands.emplace_back(std::move(operation));
                }

                platform->WakeEventLoop();
                return true;
            }

            [[nodiscard]] bool IsAccepting() const noexcept
            {
                std::scoped_lock lock(mutex);
                return accepting;
            }

            std::unique_ptr<IWindowPlatform> platform;
            std::shared_ptr<IWindowPlatformEventSink> sink;
            mutable std::mutex mutex;
            std::deque<std::function<void()>> commands;
            bool accepting { true };
            bool stopping { false };

            static thread_local WorkerState* currentWorker;
        };

        Impl(
            std::unique_ptr<IWindowPlatform> platform,
            std::shared_ptr<IWindowPlatformEventSink> sink)
            : state(std::make_shared<WorkerState>(
                  std::move(platform),
                  std::move(sink))),
              worker(
                  [state = state]
                  {
                      state->Run();
                  })
        {
        }

        ~Impl()
        {
            Stop();
        }

        void Stop() noexcept
        {
            {
                std::scoped_lock lock(state->mutex);
                state->accepting = false;
                state->stopping = true;
            }

            state->platform->WakeEventLoop();

            if (!worker.joinable())
            {
                return;
            }

            if (WorkerState::currentWorker == state.get())
            {
                // The shared WorkerState captured by the thread keeps the
                // platform and event sink wiring alive through Run's exit.
                worker.detach();
            }
            else
            {
                worker.join();
            }
        }

        std::shared_ptr<WorkerState> state;
        std::thread worker;
    };

    thread_local WindowCommandDispatcher::Impl::WorkerState*
        WindowCommandDispatcher::Impl::WorkerState::currentWorker = nullptr;

    WindowCommandDispatcher::WindowCommandDispatcher(
        std::unique_ptr<IWindowPlatform> platform,
        std::shared_ptr<IWindowPlatformEventSink> sink)
        : impl(std::make_unique<Impl>(
              std::move(platform),
              std::move(sink)))
    {
    }

    WindowCommandDispatcher::~WindowCommandDispatcher() = default;

    void WindowCommandDispatcher::Stop() noexcept
    {
        impl->Stop();
    }

    bool WindowCommandDispatcher::IsDispatcherThread() const noexcept
    {
        return Impl::WorkerState::currentWorker == impl->state.get();
    }

    bool WindowCommandDispatcher::Post(
        std::function<void()> operation) noexcept
    {
        return impl->state->Post(std::move(operation));
    }

    IWindowPlatform& WindowCommandDispatcher::Platform() noexcept
    {
        return *impl->state->platform;
    }

    bool WindowCommandDispatcher::IsAccepting() const noexcept
    {
        return impl->state->IsAccepting();
    }

    bool WindowCommandDispatcher::Dispatch(
        std::function<void()> operation) noexcept
    {
        return impl->state->Dispatch(std::move(operation));
    }
}
