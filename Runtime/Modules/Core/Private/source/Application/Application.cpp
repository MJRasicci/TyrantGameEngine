#include "TGE/Application/Application.hpp"

#include <atomic>
#include <exception>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Logging/ILogDispatcher.hpp"
#include "TGE/Logging/Logger.hpp"
#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceProvider.hpp"

#include "Internal/Logging/GlobalLogger.hpp"

namespace TGE
{
    struct Application::Impl
    {
        Impl()
            : lifetime(std::make_shared<ApplicationLifetime>())
        {
            services.AddSingleton(lifetime);
        }

        void AddDefaultServices()
        {
            services.TryAddSingleton<ILogDispatcher, GlobalLogger>();
            services.TryAddTransient<Logger<Application>>();
        }

        std::atomic<ApplicationState> state { ApplicationState::Created };
        ServiceCollection services;
        std::shared_ptr<ApplicationLifetime> lifetime;
        std::shared_ptr<ServiceProvider> provider;
        std::shared_ptr<Logger<Application>> logger;
    };

    Application::Application()
        : impl(std::make_unique<Impl>())
    {
    }

    Application::~Application()
    {
        const auto state = impl->state.load();
        if (state == ApplicationState::Starting ||
            state == ApplicationState::Running)
        {
            impl->lifetime->RequestStop();
        }
    }

    Application Application::Create()
    {
        return Application {};
    }

    ServiceCollection& Application::Services() &
    {
        if (impl->state.load() != ApplicationState::Created)
        {
            throw std::logic_error(
                "Application services cannot be changed after execution starts.");
        }

        return impl->services;
    }

    int Application::Run() &
    {
        auto result = Execution::SyncWait(RunAsync());

        if (!result)
        {
            throw std::runtime_error(
                "Application execution completed through sender cancellation.");
        }

        return std::get<0>(*result);
    }

    Task<int> Application::RunAsync() &
    {
        auto expected = ApplicationState::Created;
        if (!impl->state.compare_exchange_strong(
                expected, ApplicationState::Starting))
        {
            throw std::logic_error("Application instances can only be run once.");
        }

        std::vector<std::shared_ptr<IHostedService>> startedServices;
        std::exception_ptr failure;

        try
        {
            impl->AddDefaultServices();
            impl->provider = impl->services.BuildServiceProvider();
            impl->logger =
                impl->provider->GetRequiredService<Logger<Application>>();

            impl->logger->Debug("Configured application service provider");

            auto hostedServices = impl->provider->GetHostedServices();
            startedServices.reserve(hostedServices.size());

            const auto stopping = impl->lifetime->GetStoppingToken();
            for (const auto& service : hostedServices)
            {
                if (stopping.stop_requested())
                {
                    break;
                }

                co_await Execution::StoppedAsError(
                    service->StartAsync(stopping),
                    std::make_exception_ptr(std::runtime_error(
                        "Hosted service startup completed through cancellation.")));
                startedServices.emplace_back(service);
            }

            if (!impl->lifetime->IsStopRequested())
            {
                impl->logger->Debug("Application started");
                co_await impl->lifetime->WaitForStopAsync(
                    impl.get(),
                    [](void* context) noexcept
                    {
                        static_cast<Impl*>(context)->state.store(
                            ApplicationState::Running);
                    });
            }
        }
        catch (...)
        {
            failure = std::current_exception();
            impl->lifetime->RequestStop(1);
        }

        impl->state.store(ApplicationState::Stopping);

        try
        {
            if (impl->logger)
            {
                impl->logger->Debug("Stopping application");
            }
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }

        for (auto service = startedServices.rbegin();
             service != startedServices.rend();
             ++service)
        {
            try
            {
                co_await Execution::StoppedAsError(
                    (*service)->StopAsync(),
                    std::make_exception_ptr(std::runtime_error(
                        "Hosted service shutdown completed through cancellation.")));
            }
            catch (...)
            {
                if (!failure)
                {
                    failure = std::current_exception();
                }
            }
        }

        try
        {
            if (impl->logger)
            {
                impl->logger->Debug("Application stopped");
            }

            if (impl->provider)
            {
                if (auto dispatcher =
                        impl->provider->TryGetService<ILogDispatcher>())
                {
                    dispatcher->Flush();
                }
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
            impl->state.store(ApplicationState::Failed);
            std::rethrow_exception(failure);
        }

        impl->state.store(ApplicationState::Stopped);
        co_return impl->lifetime->GetExitCode();
    }

    bool Application::RequestStop(int exitCode) noexcept
    {
        return impl->lifetime->RequestStop(exitCode);
    }

    ApplicationState Application::GetState() const noexcept
    {
        return impl->state.load();
    }
}
