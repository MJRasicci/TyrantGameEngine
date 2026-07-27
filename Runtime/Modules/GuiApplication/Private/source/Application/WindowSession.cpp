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
#include "TGE/Input/IInputContext.hpp"
#include "TGE/Input/IInputManager.hpp"
#include "TGE/Services/ServiceProvider.hpp"

namespace
{
    bool IsIdempotentWindowError(const TGE::WindowError& error) noexcept
    {
        return error.code == TGE::WindowErrorCode::WindowDestroyed ||
               error.code == TGE::WindowErrorCode::ManagerStopped;
    }

    bool IsIdempotentInputError(const TGE::InputError& error) noexcept
    {
        return error.code == TGE::InputErrorCode::ContextDestroyed ||
               error.code == TGE::InputErrorCode::ContextNotFound ||
               error.code == TGE::InputErrorCode::ManagerStopped;
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

    TGE::Task<void> DestroyInputContextIfLive(
        std::weak_ptr<TGE::IInputManager> weakManager,
        std::weak_ptr<TGE::IInputContext> weakContext,
        TGE::InputContextId id)
    {
        auto context = weakContext.lock();
        if (!context ||
            context->LifecycleState() ==
                TGE::InputContextLifecycleState::Destroyed)
        {
            co_return;
        }

        auto manager = weakManager.lock();
        if (!manager)
        {
            co_return;
        }

        auto result = co_await manager->DestroyContextAsync(id);
        if (!result && !IsIdempotentInputError(result.error()))
        {
            throw std::runtime_error(std::format(
                "Failed to destroy input context {} while ending its "
                "window session scope: {}",
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
        std::shared_ptr<IInputManager> inputManager;
        std::shared_ptr<IInputContext> inputContext;
        std::weak_ptr<ServiceScope> scope;
        mutable std::mutex scopeMutex;
        std::shared_ptr<ServiceScope> ownedScope;
        WindowSessionOptions options;
        std::weak_ptr<ApplicationLifetime> lifetime;
        WindowSubscription closedSubscription;
        std::atomic<bool> scopeEndQueued { false };

        std::shared_ptr<ServiceScope> ReleaseOwnedScope()
        {
            std::scoped_lock lock(scopeMutex);
            return std::exchange(ownedScope, {});
        }

        void OnClosed()
        {
            if (options.scopePolicy == WindowScopePolicy::WindowOwned)
            {
                if (!scopeEndQueued.exchange(true))
                {
                    auto endingScope = ReleaseOwnedScope();
                    GetScopeEndDispatcher().Post(
                        std::move(endingScope),
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
        std::shared_ptr<ApplicationLifetime> lifetime,
        std::shared_ptr<IInputManager> inputManager,
        std::shared_ptr<IInputContext> inputContext)
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
        if (static_cast<bool>(inputManager) !=
            static_cast<bool>(inputContext))
        {
            throw std::invalid_argument(
                "A window session requires both an input manager and input "
                "context when input is attached.");
        }

        auto state = std::make_shared<State>();
        state->manager = std::move(manager);
        state->window = std::move(window);
        state->inputManager = std::move(inputManager);
        state->inputContext = std::move(inputContext);
        state->scope = scope;
        if (options.scopePolicy == WindowScopePolicy::WindowOwned)
        {
            state->ownedScope = scope;
        }
        state->options = options;
        state->lifetime = std::move(lifetime);

        const auto id = state->window->Id();
        std::weak_ptr<IWindowManager> weakManager = state->manager;
        std::weak_ptr<IWindow> weakWindow = state->window;
        scope->RegisterCleanup(
            [weakManager, weakWindow, id]() mutable -> Task<void>
            {
                co_await DestroyWindowIfLive(
                    std::move(weakManager),
                    std::move(weakWindow),
                    id);
            });

        // Scope cleanups execute in reverse registration order. Register input
        // after the window so input routing is detached before native window
        // destruction.
        if (state->inputContext)
        {
            const auto inputId = state->inputContext->Id();
            std::weak_ptr<IInputManager> weakInputManager =
                state->inputManager;
            std::weak_ptr<IInputContext> weakInputContext =
                state->inputContext;
            scope->RegisterCleanup(
                [
                    weakInputManager,
                    weakInputContext,
                    inputId
                ]() mutable -> Task<void>
                {
                    co_await DestroyInputContextIfLive(
                        std::move(weakInputManager),
                        std::move(weakInputContext),
                        inputId);
                });
        }

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

    std::shared_ptr<IInputContext> WindowSession::InputContext()
        const noexcept
    {
        return state->inputContext;
    }

    std::shared_ptr<ServiceScope> WindowSession::Scope() const noexcept
    {
        return state->scope.lock();
    }

    WindowScopePolicy WindowSession::ScopePolicy() const noexcept
    {
        return state->options.scopePolicy;
    }

    Task<void> WindowSession::EndAsync()
    {
        return EndTask(state);
    }

    Task<void> WindowSession::EndTask(
        std::shared_ptr<State> keepAlive)
    {
        if (keepAlive->options.scopePolicy ==
            WindowScopePolicy::WindowOwned)
        {
            auto endingScope = keepAlive->ReleaseOwnedScope();
            if (!endingScope)
            {
                endingScope = keepAlive->scope.lock();
            }
            if (endingScope)
            {
                co_await endingScope->EndAsync();
            }
            co_return;
        }

        if (keepAlive->inputContext)
        {
            co_await DestroyInputContextIfLive(
                keepAlive->inputManager,
                keepAlive->inputContext,
                keepAlive->inputContext->Id());
        }

        co_await DestroyWindowIfLive(
            keepAlive->manager,
            keepAlive->window,
            keepAlive->window->Id());
    }
}
