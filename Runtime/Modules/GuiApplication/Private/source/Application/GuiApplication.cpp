#include "TGE/Application/GuiApplication.hpp"

#include <atomic>
#include <exception>
#include <format>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "Internal/Desktop/DesktopEventRuntime.hpp"
#include "Internal/Graphics/WindowManager.hpp"
#include "Internal/Input/IInputPlatform.hpp"
#include "Internal/Input/InputManager.hpp"
#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Application/GuiApplicationContext.hpp"
#include "TGE/Application/IWindowInputContextFactory.hpp"
#include "TGE/Application/WindowSession.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Graphics/IWindow.hpp"
#include "TGE/Graphics/IWindowManager.hpp"
#include "TGE/Input/IInputManager.hpp"
#include "TGE/Services/ServiceLocator.hpp"
#include "TGE/Services/ServiceProvider.hpp"
#include "TGE/Services/ServiceTraits.hpp"

#if TGE_HAS_SDL_DESKTOP_BACKEND
#include "Internal/SDLDesktop/SDLDesktopFactory.hpp"
#endif
#if TGE_HAS_VULKAN_PRESENTATION
#include "Internal/VulkanPresentation/VulkanWindowPresenterFactory.hpp"
#endif

namespace TGE::detail
{
    class DefaultWindowInputContextFactory final
        : public IWindowInputContextFactory
    {
    public:
        explicit DefaultWindowInputContextFactory(
            std::shared_ptr<Internal::InputManager> manager)
            : manager(std::move(manager))
        {
        }

        Task<InputContextResult> CreateForWindowAsync(
            std::shared_ptr<IWindow> window,
            InputContextDescriptor descriptor) override
        {
            return CreateForWindowTask(
                manager,
                std::move(window),
                std::move(descriptor));
        }

    private:
        static Task<InputContextResult> CreateForWindowTask(
            std::shared_ptr<Internal::InputManager> manager,
            std::shared_ptr<IWindow> window,
            InputContextDescriptor descriptor)
        {
            if (!window ||
                window->LifecycleState() != WindowLifecycleState::Open)
            {
                co_return std::unexpected(InputError {
                    .code = InputErrorCode::InvalidDescriptor,
                    .message =
                        "An input context requires a live engine window."
                });
            }

            co_return co_await manager->CreateContextForTargetAsync(
                std::move(descriptor),
                Internal::InputPlatformTarget::FromValue(
                    window->Id().Value()));
        }

        std::shared_ptr<Internal::InputManager> manager;
    };

    struct DesktopBackend
    {
        std::shared_ptr<Internal::DesktopEventRuntime> runtime;
        std::shared_ptr<Internal::WindowManager> windowManager;
        std::shared_ptr<Internal::InputManager> inputManager;
        std::atomic<bool> stopped { false };

        void Shutdown()
        {
            if (stopped.exchange(true))
            {
                return;
            }

            std::exception_ptr failure;
            try
            {
                auto completion = Execution::SyncWait(
                    inputManager->ShutdownAsync());
                if (!completion)
                {
                    throw std::runtime_error(
                        "Input backend shutdown was cancelled.");
                }
            }
            catch (...)
            {
                failure = std::current_exception();
            }

            try
            {
                auto completion = Execution::SyncWait(
                    windowManager->ShutdownAsync());
                if (!completion)
                {
                    throw std::runtime_error(
                        "Window backend shutdown was cancelled.");
                }
            }
            catch (...)
            {
                if (!failure)
                {
                    failure = std::current_exception();
                }
            }

            if (failure)
            {
                std::rethrow_exception(failure);
            }
        }
    };

    struct GuiApplicationConfiguration
    {
        std::mutex mutex;
        std::optional<WindowDescriptor> rootDescriptor;
        std::shared_ptr<DesktopBackend> desktopBackend;
    };

    class GuiApplicationCoordinator final : public IHostedService
    {
    public:
        GuiApplicationCoordinator(
            std::shared_ptr<GuiApplicationConfiguration> configuration,
            std::shared_ptr<GuiApplicationContext> context,
            std::shared_ptr<ApplicationLifetime> lifetime,
            ServiceLocator& locator)
            : configuration(std::move(configuration)),
              context(std::move(context)),
              lifetime(std::move(lifetime)),
              locator(locator)
        {
        }

        Task<void> StartAsync(std::stop_token) override
        {
            std::optional<WindowDescriptor> descriptor;
            {
                std::scoped_lock lock(configuration->mutex);
                descriptor = configuration->rootDescriptor;
            }

            if (!descriptor)
            {
                co_return;
            }

            auto manager = locator.TryGetService<IWindowManager>();
            if (!manager)
            {
                throw std::domain_error(
                    "GuiApplication requires an IWindowManager service when "
                    "a root window is configured.");
            }

            auto* provider = dynamic_cast<ServiceProvider*>(&locator);
            if (!provider)
            {
                throw std::logic_error(
                    "GuiApplication must be activated from the root "
                    "Application service provider.");
            }

            auto creation =
                co_await manager->CreateWindowAsync(*descriptor);
            if (!creation)
            {
                throw std::runtime_error(std::format(
                    "Failed to create the GuiApplication root window: {}",
                    creation.error().message));
            }

            const auto window = *creation;
            std::shared_ptr<IInputManager> inputManager;
            std::shared_ptr<IInputContext> inputContext;
            std::exception_ptr compositionFailure;
            try
            {
                auto inputFactory =
                    locator.TryGetService<IWindowInputContextFactory>();
                if (inputFactory)
                {
                    inputManager = locator.TryGetService<IInputManager>();
                    if (!inputManager)
                    {
                        throw std::domain_error(
                            "A window input-context factory requires an "
                            "IInputManager registration.");
                    }

                    auto input =
                        co_await inputFactory->CreateForWindowAsync(
                            window,
                            InputContextDescriptor {
                                .name = descriptor->title.empty()
                                    ? "GuiApplication root window"
                                    : descriptor->title
                            });
                    if (!input)
                    {
                        throw std::runtime_error(std::format(
                            "Failed to create the GuiApplication root input "
                            "context: {}",
                            input.error().message));
                    }
                    inputContext = std::move(*input);
                }

                auto rootScope = provider->CreateScope();
                auto rootSession = WindowSession::Create(
                    manager,
                    window,
                    std::move(rootScope),
                    WindowSessionOptions {
                        .scopePolicy = WindowScopePolicy::WindowOwned,
                        .stopApplicationOnClose = true
                    },
                    lifetime,
                    inputManager,
                    inputContext);

                context->PublishRootSession(std::move(rootSession));
            }
            catch (...)
            {
                compositionFailure = std::current_exception();
            }

            if (!compositionFailure)
            {
                co_return;
            }

            if (inputManager && inputContext)
            {
                try
                {
                    (void)co_await inputManager->DestroyContextAsync(
                        inputContext->Id());
                }
                catch (...)
                {
                }
            }
            try
            {
                (void)co_await manager->DestroyWindowAsync(
                    window->Id());
            }
            catch (...)
            {
            }
            std::rethrow_exception(compositionFailure);
        }

        Task<void> StopAsync() override
        {
            auto rootSession = context->ClearRootSession();

            if (rootSession)
            {
                co_await rootSession->EndAsync();
            }
        }

    private:
        std::shared_ptr<GuiApplicationConfiguration> configuration;
        std::shared_ptr<GuiApplicationContext> context;
        std::shared_ptr<ApplicationLifetime> lifetime;
        ServiceLocator& locator;
    };
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    TGE::detail::GuiApplicationCoordinator,
    TGE::Inject<TGE::detail::GuiApplicationConfiguration>(),
    TGE::Inject<TGE::GuiApplicationContext>(),
    TGE::Inject<TGE::ApplicationLifetime>(),
    TGE::InjectLocator());
#endif

namespace TGE
{
    GuiApplication::GuiApplication()
        : application(Application::Create()),
          configuration(
              std::make_shared<detail::GuiApplicationConfiguration>()),
          context(std::make_shared<GuiApplicationContext>())
    {
        application.Services().AddSingleton(configuration);
        application.Services().AddSingleton(context);
        application.AddHostedService<detail::GuiApplicationCoordinator>();
    }

    GuiApplication::~GuiApplication() = default;

    GuiApplication GuiApplication::Create()
    {
        return GuiApplication {};
    }

    ServiceCollection& GuiApplication::Services() &
    {
        return application.Services();
    }

    GuiApplication& GuiApplication::ConfigureRootWindow(
        WindowDescriptor descriptor) &
    {
        // Validate that configuration remains mutable before publishing it.
        (void)application.Services();

        std::scoped_lock lock(configuration->mutex);
        configuration->rootDescriptor = std::move(descriptor);
        return *this;
    }

    GuiApplication& GuiApplication::UseDefaultDesktopBackend() &
    {
        auto& services = application.Services();

#if TGE_HAS_SDL_DESKTOP_BACKEND
        {
            std::scoped_lock lock(configuration->mutex);
            if (configuration->desktopBackend)
            {
                throw std::logic_error(
                    "The default desktop backend is already configured.");
            }
        }

        if (services.Contains<IWindowManager>() ||
            services.Contains<IInputManager>() ||
            services.Contains<IWindowInputContextFactory>())
        {
            throw std::domain_error(
                "The default desktop backend cannot replace existing window, "
                "input, or window-input integration services.");
        }

        auto components = Internal::CreateSDLDesktopComponents();
        auto runtime =
            std::make_shared<Internal::DesktopEventRuntime>(
                std::move(components.eventPump));
#if TGE_HAS_VULKAN_PRESENTATION
        auto windowManager =
            std::make_shared<Internal::WindowManager>(
                runtime,
                std::move(components.windowPlatform),
                std::move(components.presentationTargetProvider),
                Internal::CreateVulkanWindowPresenter());
#else
        auto windowManager =
            std::make_shared<Internal::WindowManager>(
                runtime,
                std::move(components.windowPlatform));
#endif
        auto inputManager =
            std::make_shared<Internal::InputManager>(
                runtime,
                std::move(components.inputPlatform));
        auto inputFactory =
            std::make_shared<detail::DefaultWindowInputContextFactory>(
                inputManager);

        services.AddSingleton<IWindowManager>(windowManager);
        services.AddSingleton<IInputManager>(inputManager);
        services.AddSingleton<IWindowInputContextFactory>(inputFactory);

        auto backend = std::make_shared<detail::DesktopBackend>();
        backend->runtime = std::move(runtime);
        backend->windowManager = std::move(windowManager);
        backend->inputManager = std::move(inputManager);

        {
            std::scoped_lock lock(configuration->mutex);
            configuration->desktopBackend = std::move(backend);
        }
        return *this;
#else
        (void)services;
        throw std::domain_error(
            "This Tyrant runtime was built without a default desktop backend.");
#endif
    }

    GuiApplication& GuiApplication::ClearRootWindow() &
    {
        (void)application.Services();

        std::scoped_lock lock(configuration->mutex);
        configuration->rootDescriptor.reset();
        return *this;
    }

    std::shared_ptr<WindowSession> GuiApplication::RootSession() const
    {
        return context->RootSession();
    }

    int GuiApplication::Run() &
    {
        std::shared_ptr<detail::DesktopBackend> backend;
        {
            std::scoped_lock lock(configuration->mutex);
            backend = configuration->desktopBackend;
        }

        if (!backend)
        {
            return application.Run();
        }

        std::promise<int> completion;
        auto result = completion.get_future();
        std::jthread applicationThread(
            [this, backend, completion = std::move(completion)]() mutable
            {
                std::exception_ptr failure;
                int exitCode = 1;

                try
                {
                    exitCode = application.Run();
                }
                catch (...)
                {
                    failure = std::current_exception();
                }

                try
                {
                    backend->Shutdown();
                }
                catch (...)
                {
                    if (!failure)
                    {
                        failure = std::current_exception();
                    }
                }

                backend->runtime->RequestStop();

                if (failure)
                {
                    completion.set_exception(std::move(failure));
                }
                else
                {
                    completion.set_value(exitCode);
                }
            });

        const auto pump = backend->runtime->Run();
        if (!pump)
        {
            application.RequestStop(1);
        }

        applicationThread.join();

        if (!pump)
        {
            // Consume the lifecycle result so an exception stored by the
            // application worker cannot be abandoned.
            try
            {
                (void)result.get();
            }
            catch (...)
            {
            }

            throw std::runtime_error(
                pump.error().message.empty()
                    ? "The default desktop event pump failed to start."
                    : pump.error().message);
        }

        return result.get();
    }

    Task<int> GuiApplication::RunAsync() &
    {
        co_return Run();
    }

    bool GuiApplication::RequestStop(int exitCode) noexcept
    {
        return application.RequestStop(exitCode);
    }

    ApplicationState GuiApplication::GetState() const noexcept
    {
        return application.GetState();
    }
}
