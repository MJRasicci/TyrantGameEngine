#include <gtest/gtest.h>

#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Application/GuiApplication.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Application/WindowSession.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Graphics/IWindow.hpp"
#include "TGE/Graphics/IWindowManager.hpp"
#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceProvider.hpp"
#include "TGE/Services/ServiceTraits.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    template<class T>
    T WaitFor(TGE::Task<T> task)
    {
        auto completion = TGE::Execution::SyncWait(std::move(task));
        if (!completion)
        {
            throw std::runtime_error("Test task completed through cancellation.");
        }
        return std::get<0>(std::move(*completion));
    }

    void WaitFor(TGE::Task<void> task)
    {
        auto completion = TGE::Execution::SyncWait(std::move(task));
        if (!completion)
        {
            throw std::runtime_error("Test task completed through cancellation.");
        }
    }

    class TestWindow final
        : public TGE::IWindow,
          public std::enable_shared_from_this<TestWindow>
    {
    public:
        TestWindow(TGE::WindowId id, TGE::WindowDescriptor descriptor)
            : id(id),
              descriptor(std::move(descriptor))
        {
            configuration.title = this->descriptor.title;
            configuration.role = this->descriptor.role;
            configuration.parent = this->descriptor.parent;
            configuration.geometry.logicalBounds = this->descriptor.bounds;
            configuration.state = this->descriptor.state;
            configuration.chrome = this->descriptor.chrome;
            configuration.modality = this->descriptor.modality;
            configuration.visible = this->descriptor.initiallyVisible;
            configuration.alwaysOnTop = this->descriptor.alwaysOnTop;
            configuration.inputEnabled = this->descriptor.acceptsInput;
        }

        TGE::WindowId Id() const noexcept override
        {
            return id;
        }

        TGE::WindowDescriptor RequestedDescriptor() const override
        {
            return descriptor;
        }

        TGE::WindowConfiguration EffectiveConfiguration() const override
        {
            return configuration;
        }

        TGE::WindowCapabilities Capabilities() const noexcept override
        {
            return {};
        }

        TGE::WindowLifecycleState LifecycleState() const noexcept override
        {
            return lifecycle.load();
        }

        std::string Title() const override
        {
            return configuration.title;
        }

        TGE::WindowRole Role() const noexcept override
        {
            return configuration.role;
        }

        std::optional<TGE::WindowId> ParentId() const noexcept override
        {
            return configuration.parent;
        }

        TGE::WindowGeometry Geometry() const noexcept override
        {
            return configuration.geometry;
        }

        TGE::WindowState State() const noexcept override
        {
            return configuration.state;
        }

        bool IsVisible() const noexcept override
        {
            return configuration.visible;
        }

        bool IsFocused() const noexcept override
        {
            return configuration.focused;
        }

        bool IsInputEnabled() const noexcept override
        {
            return configuration.inputEnabled;
        }

        TGE::Task<TGE::WindowOperationResult> SetTitleAsync(
            std::string title) override
        {
            configuration.title = std::move(title);
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::Task<TGE::WindowOperationResult> SetLogicalBoundsAsync(
            TGE::LogicalBounds bounds) override
        {
            configuration.geometry.logicalBounds = bounds;
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::Task<TGE::WindowOperationResult> SetStateAsync(
            TGE::WindowState state) override
        {
            configuration.state = state;
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::Task<TGE::WindowOperationResult> ShowAsync() override
        {
            configuration.visible = true;
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::Task<TGE::WindowOperationResult> HideAsync() override
        {
            configuration.visible = false;
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::Task<TGE::WindowOperationResult> RequestFocusAsync() override
        {
            configuration.focused = true;
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::Task<TGE::WindowOperationResult> SetInputEnabledAsync(
            bool enabled) override
        {
            configuration.inputEnabled = enabled;
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::Task<TGE::WindowOperationResult> RequestCloseAsync() override
        {
            EmitClosed(TGE::WindowCloseReason::ApplicationRequest);
            co_return TGE::WindowOperationStatus::Applied;
        }

        TGE::WindowSubscription SubscribeCloseRequested(
            TGE::WindowCloseRequestedCallback) override
        {
            return {};
        }

        TGE::WindowSubscription SubscribeClosed(
            TGE::WindowClosedCallback callback) override
        {
            std::size_t subscription;
            {
                std::scoped_lock lock(callbackMutex);
                subscription = nextSubscription++;
                closedCallbacks.emplace(subscription, std::move(callback));
            }

            std::weak_ptr<TestWindow> weak = shared_from_this();
            return TGE::WindowSubscription(
                [weak, subscription]
                {
                    if (auto window = weak.lock())
                    {
                        std::scoped_lock lock(window->callbackMutex);
                        window->closedCallbacks.erase(subscription);
                    }
                });
        }

        TGE::WindowSubscription SubscribeMoved(
            TGE::WindowMovedCallback) override
        {
            return {};
        }

        TGE::WindowSubscription SubscribeResized(
            TGE::WindowResizedCallback) override
        {
            return {};
        }

        TGE::WindowSubscription SubscribeScaleChanged(
            TGE::WindowScaleChangedCallback) override
        {
            return {};
        }

        TGE::WindowSubscription SubscribeStateChanged(
            TGE::WindowStateChangedCallback) override
        {
            return {};
        }

        TGE::WindowSubscription SubscribeFocusChanged(
            TGE::WindowFocusChangedCallback) override
        {
            return {};
        }

        TGE::WindowSubscription SubscribeInputChanged(
            TGE::WindowInputChangedCallback) override
        {
            return {};
        }

        TGE::WindowSubscription SubscribeConfigurationChanged(
            TGE::WindowConfigurationChangedCallback) override
        {
            return {};
        }

        void EmitClosed(TGE::WindowCloseReason reason)
        {
            if (lifecycle.exchange(
                    TGE::WindowLifecycleState::Destroyed) ==
                TGE::WindowLifecycleState::Destroyed)
            {
                return;
            }

            std::vector<TGE::WindowClosedCallback> callbacks;
            {
                std::scoped_lock lock(callbackMutex);
                callbacks.reserve(closedCallbacks.size());
                for (const auto& [_, callback] : closedCallbacks)
                {
                    callbacks.emplace_back(callback);
                }
            }

            const TGE::WindowClosedEvent event {
                .window = id,
                .reason = reason
            };
            for (const auto& callback : callbacks)
            {
                callback(event);
            }
        }

    private:
        TGE::WindowId id;
        TGE::WindowDescriptor descriptor;
        TGE::WindowConfiguration configuration;
        std::atomic<TGE::WindowLifecycleState> lifecycle {
            TGE::WindowLifecycleState::Open
        };
        std::mutex callbackMutex;
        std::size_t nextSubscription { 1 };
        std::unordered_map<std::size_t, TGE::WindowClosedCallback>
            closedCallbacks;
    };

    class TestWindowManager final : public TGE::IWindowManager
    {
    public:
        TGE::Task<TGE::WindowResult> CreateWindowAsync(
            TGE::WindowDescriptor descriptor) override
        {
            auto window = std::make_shared<TestWindow>(
                TGE::WindowId::FromValue(nextId++),
                std::move(descriptor));
            {
                std::scoped_lock lock(mutex);
                windows.emplace_back(window);
            }
            creations.fetch_add(1);
            co_return std::shared_ptr<TGE::IWindow>(std::move(window));
        }

        std::shared_ptr<TGE::IWindow> FindWindow(
            TGE::WindowId id) const noexcept override
        {
            std::scoped_lock lock(mutex);
            for (const auto& window : windows)
            {
                if (window->Id() == id)
                {
                    return window;
                }
            }
            return {};
        }

        std::vector<std::shared_ptr<TGE::IWindow>> Windows()
            const override
        {
            std::scoped_lock lock(mutex);
            return windows;
        }

        TGE::Task<TGE::WindowOperationResult> DestroyWindowAsync(
            TGE::WindowId id) override
        {
            auto window =
                std::dynamic_pointer_cast<TestWindow>(FindWindow(id));
            if (!window ||
                window->LifecycleState() ==
                    TGE::WindowLifecycleState::Destroyed)
            {
                co_return std::unexpected(TGE::WindowError {
                    .code = TGE::WindowErrorCode::WindowDestroyed,
                    .message = "already destroyed"
                });
            }

            destructions.fetch_add(1);
            window->EmitClosed(TGE::WindowCloseReason::ApplicationRequest);
            co_return TGE::WindowOperationStatus::Applied;
        }

        std::atomic<int> creations { 0 };
        std::atomic<int> destructions { 0 };

    private:
        mutable std::mutex mutex;
        std::vector<std::shared_ptr<TGE::IWindow>> windows;
        std::uint64_t nextId { 1 };
    };

    class StopApplicationService final : public TGE::IHostedService
    {
    public:
        explicit StopApplicationService(
            std::shared_ptr<TGE::ApplicationLifetime> lifetime)
            : lifetime(std::move(lifetime))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            lifetime->RequestStop();
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            co_return;
        }

    private:
        std::shared_ptr<TGE::ApplicationLifetime> lifetime;
    };

    class CloseRootService final : public TGE::IHostedService
    {
    public:
        explicit CloseRootService(
            std::shared_ptr<TGE::IWindowManager> manager)
            : manager(std::move(manager))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            const auto windows = manager->Windows();
            if (windows.empty())
            {
                throw std::runtime_error(
                    "Root window was not created before user services.");
            }

            auto result =
                co_await windows.front()->RequestCloseAsync();
            if (!result)
            {
                throw std::runtime_error(result.error().message);
            }
        }

        TGE::Task<void> StopAsync() override
        {
            co_return;
        }

    private:
        std::shared_ptr<TGE::IWindowManager> manager;
    };

    std::shared_ptr<TestWindow> CreateTestWindow(
        const std::shared_ptr<TestWindowManager>& manager)
    {
        auto result = WaitFor(
            manager->CreateWindowAsync(TGE::WindowDescriptor {
                .title = "Session"
            }));
        if (!result)
        {
            throw std::runtime_error(result.error().message);
        }
        return std::dynamic_pointer_cast<TestWindow>(*result);
    }
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    StopApplicationService,
    TGE::Inject<TGE::ApplicationLifetime>());
TGE_DECLARE_SERVICE_DEPENDENCIES(
    CloseRootService,
    TGE::Inject<TGE::IWindowManager>());
#endif

TEST(GuiApplicationTests, DoesNotRequireWindowManagerWithoutRootConfiguration)
{
    auto application = TGE::GuiApplication::Create();
    application.AddHostedService<StopApplicationService>();

    EXPECT_EQ(application.Run(), 0);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Stopped);
}

TEST(GuiApplicationTests, CreatesRootBeforeUserServicesAndStopsWhenItCloses)
{
    auto application = TGE::GuiApplication::Create();
    auto manager = std::make_shared<TestWindowManager>();
    std::shared_ptr<TGE::IWindowManager> managerService = manager;

    application.Services().AddSingleton(managerService);
    application.ConfigureRootWindow(TGE::WindowDescriptor {
        .title = "Root"
    });
    application.AddHostedService<CloseRootService>();

    EXPECT_EQ(application.Run(), 0);
    EXPECT_EQ(manager->creations.load(), 1);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Stopped);
}

TEST(GuiApplicationTests, ApplicationShutdownEndsRootScopeAndDestroysWindow)
{
    auto application = TGE::GuiApplication::Create();
    auto manager = std::make_shared<TestWindowManager>();
    std::shared_ptr<TGE::IWindowManager> managerService = manager;

    application.Services().AddSingleton(managerService);
    application.ConfigureRootWindow(TGE::WindowDescriptor {
        .title = "Root"
    });
    application.AddHostedService<StopApplicationService>();

    EXPECT_EQ(application.Run(), 0);
    EXPECT_EQ(manager->creations.load(), 1);
    EXPECT_EQ(manager->destructions.load(), 1);
    EXPECT_EQ(application.RootSession(), nullptr);
}

TEST(WindowSessionTests, EndingScopeDestroysItsLiveWindow)
{
    TGE::ServiceCollection services;
    auto provider = services.BuildServiceProvider();
    auto scope = provider->CreateScope();
    auto manager = std::make_shared<TestWindowManager>();
    auto window = CreateTestWindow(manager);

    auto session = TGE::WindowSession::Create(
        manager,
        window,
        scope,
        TGE::WindowSessionOptions {
            .scopePolicy = TGE::WindowScopePolicy::External
        });

    WaitFor(scope->EndAsync());

    EXPECT_EQ(manager->destructions.load(), 1);
    EXPECT_EQ(
        window->LifecycleState(),
        TGE::WindowLifecycleState::Destroyed);
}

TEST(WindowSessionTests, InheritedWindowClosureDoesNotEndSharedScope)
{
    TGE::ServiceCollection services;
    auto provider = services.BuildServiceProvider();
    auto scope = provider->CreateScope();
    auto manager = std::make_shared<TestWindowManager>();
    auto window = CreateTestWindow(manager);

    auto session = TGE::WindowSession::Create(
        manager,
        window,
        scope,
        TGE::WindowSessionOptions {
            .scopePolicy = TGE::WindowScopePolicy::Inherited
        });

    window->EmitClosed(TGE::WindowCloseReason::UserRequest);

    EXPECT_EQ(scope->GetState(), TGE::ServiceScopeState::Active);
}

TEST(WindowSessionTests, WindowOwnedClosureEndsScopeOffEventThread)
{
    TGE::ServiceCollection services;
    auto provider = services.BuildServiceProvider();
    auto scope = provider->CreateScope();
    auto manager = std::make_shared<TestWindowManager>();
    auto window = CreateTestWindow(manager);

    std::mutex mutex;
    std::condition_variable condition;
    bool cleaned = false;
    std::thread::id cleanupThread;
    scope->RegisterCleanup(
        [&]() -> TGE::Task<void>
        {
            {
                std::scoped_lock lock(mutex);
                cleaned = true;
                cleanupThread = std::this_thread::get_id();
            }
            condition.notify_all();
            co_return;
        });

    auto session = TGE::WindowSession::Create(
        manager,
        window,
        scope,
        TGE::WindowSessionOptions {
            .scopePolicy = TGE::WindowScopePolicy::WindowOwned
        });

    const auto eventThread = std::this_thread::get_id();
    window->EmitClosed(TGE::WindowCloseReason::UserRequest);

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&]
            {
                return cleaned;
            }));
    }

    EXPECT_NE(cleanupThread, eventThread);
    EXPECT_EQ(scope->GetState(), TGE::ServiceScopeState::Ended);
}
