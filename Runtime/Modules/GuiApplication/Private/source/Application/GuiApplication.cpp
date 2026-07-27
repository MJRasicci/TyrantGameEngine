#include "TGE/Application/GuiApplication.hpp"

#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Application/WindowSession.hpp"
#include "TGE/Graphics/IWindowManager.hpp"
#include "TGE/Services/ServiceLocator.hpp"
#include "TGE/Services/ServiceProvider.hpp"
#include "TGE/Services/ServiceTraits.hpp"

namespace TGE::detail
{
    struct GuiApplicationConfiguration
    {
        std::mutex mutex;
        std::optional<WindowDescriptor> rootDescriptor;
        std::shared_ptr<WindowSession> rootSession;
    };

    class GuiApplicationCoordinator final : public IHostedService
    {
    public:
        GuiApplicationCoordinator(
            std::shared_ptr<GuiApplicationConfiguration> configuration,
            std::shared_ptr<ApplicationLifetime> lifetime,
            ServiceLocator& locator)
            : configuration(std::move(configuration)),
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
                co_await manager->CreateWindowAsync(std::move(*descriptor));
            if (!creation)
            {
                throw std::runtime_error(std::format(
                    "Failed to create the GuiApplication root window: {}",
                    creation.error().message));
            }

            auto rootScope = provider->CreateScope();
            auto rootSession = WindowSession::Create(
                std::move(manager),
                std::move(*creation),
                std::move(rootScope),
                WindowSessionOptions {
                    .scopePolicy = WindowScopePolicy::WindowOwned,
                    .stopApplicationOnClose = true
                },
                lifetime);

            {
                std::scoped_lock lock(configuration->mutex);
                configuration->rootSession = std::move(rootSession);
            }
        }

        Task<void> StopAsync() override
        {
            std::shared_ptr<WindowSession> rootSession;
            {
                std::scoped_lock lock(configuration->mutex);
                rootSession =
                    std::exchange(configuration->rootSession, {});
            }

            if (rootSession)
            {
                co_await rootSession->EndAsync();
            }
        }

    private:
        std::shared_ptr<GuiApplicationConfiguration> configuration;
        std::shared_ptr<ApplicationLifetime> lifetime;
        ServiceLocator& locator;
    };
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    TGE::detail::GuiApplicationCoordinator,
    TGE::Inject<TGE::detail::GuiApplicationConfiguration>(),
    TGE::Inject<TGE::ApplicationLifetime>(),
    TGE::InjectLocator());
#endif

namespace TGE
{
    GuiApplication::GuiApplication()
        : application(Application::Create()),
          configuration(
              std::make_shared<detail::GuiApplicationConfiguration>())
    {
        application.Services().AddSingleton(configuration);
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

    GuiApplication& GuiApplication::ClearRootWindow() &
    {
        (void)application.Services();

        std::scoped_lock lock(configuration->mutex);
        configuration->rootDescriptor.reset();
        return *this;
    }

    std::shared_ptr<WindowSession> GuiApplication::RootSession() const
    {
        std::scoped_lock lock(configuration->mutex);
        return configuration->rootSession;
    }

    int GuiApplication::Run() &
    {
        return application.Run();
    }

    Task<int> GuiApplication::RunAsync() &
    {
        return application.RunAsync();
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
