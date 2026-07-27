#include "TGE/Application/WindowSession.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>

#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Graphics/IWindow.hpp"
#include "TGE/Graphics/IWindowManager.hpp"
#include "TGE/Services/ServiceProvider.hpp"

namespace
{
    bool IsIdempotentWindowError(const TGE::WindowError& error) noexcept
    {
        return error.code == TGE::WindowErrorCode::WindowDestroyed ||
               error.code == TGE::WindowErrorCode::ManagerStopped;
    }

    TGE::Task<void> DestroyWindowIfLive(
        std::weak_ptr<TGE::IWindowManager> weakManager,
        std::weak_ptr<TGE::IWindow> weakWindow,
        TGE::WindowId id)
    {
        auto window = weakWindow.lock();
        if (!window ||
            window->LifecycleState() == TGE::WindowLifecycleState::Destroyed)
        {
            co_return;
        }

        auto manager = weakManager.lock();
        if (!manager)
        {
            co_return;
        }

        auto result = co_await manager->DestroyWindowAsync(id);
        if (!result && !IsIdempotentWindowError(result.error()))
        {
            throw std::runtime_error(std::format(
                "Failed to destroy window {} while ending its session scope: {}",
                id.Value(),
                result.error().message));
        }
    }

    /**
     * Window callbacks run on the serialized platform dispatcher. Ending a
     * scope there could re-enter window destruction, so closure posts scope
     * termination to this process-local worker.
     */
    class ScopeEndDispatcher final
    {
    public:
        ScopeEndDispatcher()
            : worker(
                  [this](std::stop_token stopping)
                  {
                      Run(stopping);
                  })
        {
        }

        ~ScopeEndDispatcher()
        {
            worker.request_stop();
            condition.notify_all();
        }

        void Post(
            std::shared_ptr<TGE::ServiceScope> scope,
            std::weak_ptr<TGE::ApplicationLifetime> lifetime,
            bool requestApplicationStop)
        {
            {
                std::scoped_lock lock(mutex);
                pending.emplace_back(
                    Work {
                        .scope = std::move(scope),
                        .lifetime = std::move(lifetime),
                        .requestApplicationStop =
                            requestApplicationStop
                    });
            }
            condition.notify_one();
        }

    private:
        struct Work
        {
            std::shared_ptr<TGE::ServiceScope> scope;
            std::weak_ptr<TGE::ApplicationLifetime> lifetime;
            bool requestApplicationStop { false };
        };

        void Run(std::stop_token stopping) noexcept
        {
            while (!stopping.stop_requested())
            {
                Work work;
                {
                    std::unique_lock lock(mutex);
                    condition.wait(
                        lock,
                        stopping,
                        [this]
                        {
                            return !pending.empty();
                        });

                    if (pending.empty())
                    {
                        continue;
                    }

                    work = std::move(pending.front());
                    pending.pop_front();
                }

                if (work.scope)
                {
                    try
                    {
                        auto result =
                            TGE::Execution::SyncWait(
                                work.scope->EndAsync());
                        (void)result;
                    }
                    catch (...)
                    {
                        // Explicit EndAsync remains the observable path for
                        // surfacing cleanup failures. Event-driven ending must
                        // not terminate the platform dispatcher or process.
                    }
                }

                if (work.requestApplicationStop)
                {
                    if (auto lifetime = work.lifetime.lock())
                    {
                        lifetime->RequestStop();
                    }
                }
            }
        }

        std::mutex mutex;
        std::condition_variable_any condition;
        std::deque<Work> pending;
        std::jthread worker;
    };

    ScopeEndDispatcher& GetScopeEndDispatcher()
    {
        static ScopeEndDispatcher dispatcher;
        return dispatcher;
    }
}

namespace TGE
{
    struct WindowSession::State
    {
        std::shared_ptr<IWindowManager> manager;
        std::shared_ptr<IWindow> window;
        std::shared_ptr<ServiceScope> scope;
        WindowSessionOptions options;
        std::weak_ptr<ApplicationLifetime> lifetime;
        WindowSubscription closedSubscription;
        std::atomic<bool> scopeEndQueued { false };

        void OnClosed()
        {
            if (options.scopePolicy == WindowScopePolicy::WindowOwned)
            {
                if (!scopeEndQueued.exchange(true))
                {
                    GetScopeEndDispatcher().Post(
                        scope,
                        lifetime,
                        options.stopApplicationOnClose);
                }
                return;
            }

            if (options.stopApplicationOnClose)
            {
                GetScopeEndDispatcher().Post(
                    {},
                    lifetime,
                    true);
            }
        }
    };

    std::shared_ptr<WindowSession> WindowSession::Create(
        std::shared_ptr<IWindowManager> manager,
        std::shared_ptr<IWindow> window,
        std::shared_ptr<ServiceScope> scope,
        WindowSessionOptions options,
        std::shared_ptr<ApplicationLifetime> lifetime)
    {
        if (!manager)
        {
            throw std::invalid_argument(
                "A window session requires a window manager.");
        }
        if (!window)
        {
            throw std::invalid_argument(
                "A window session requires a window.");
        }
        if (!scope)
        {
            throw std::invalid_argument(
                "A window session requires a service scope.");
        }
        if (options.stopApplicationOnClose && !lifetime)
        {
            throw std::invalid_argument(
                "A root window session requires an application lifetime.");
        }

        auto state = std::make_shared<State>();
        state->manager = std::move(manager);
        state->window = std::move(window);
        state->scope = std::move(scope);
        state->options = options;
        state->lifetime = std::move(lifetime);

        const auto id = state->window->Id();
        std::weak_ptr<IWindowManager> weakManager = state->manager;
        std::weak_ptr<IWindow> weakWindow = state->window;
        state->scope->RegisterCleanup(
            [weakManager, weakWindow, id]() mutable -> Task<void>
            {
                co_await DestroyWindowIfLive(
                    std::move(weakManager),
                    std::move(weakWindow),
                    id);
            });

        std::weak_ptr<State> weakState = state;
        state->closedSubscription = state->window->SubscribeClosed(
            [weakState](const WindowClosedEvent&)
            {
                if (auto locked = weakState.lock())
                {
                    locked->OnClosed();
                }
            });

        return std::shared_ptr<WindowSession>(
            new WindowSession(std::move(state)));
    }

    WindowSession::WindowSession(std::shared_ptr<State> state)
        : state(std::move(state))
    {
    }

    WindowSession::~WindowSession() = default;

    std::shared_ptr<IWindow> WindowSession::Window() const noexcept
    {
        return state->window;
    }

    std::shared_ptr<ServiceScope> WindowSession::Scope() const noexcept
    {
        return state->scope;
    }

    WindowScopePolicy WindowSession::ScopePolicy() const noexcept
    {
        return state->options.scopePolicy;
    }

    Task<void> WindowSession::EndAsync()
    {
        auto keepAlive = state;

        if (keepAlive->options.scopePolicy ==
            WindowScopePolicy::WindowOwned)
        {
            co_await keepAlive->scope->EndAsync();
            co_return;
        }

        co_await DestroyWindowIfLive(
            keepAlive->manager,
            keepAlive->window,
            keepAlive->window->Id());
    }
}
