#include "TGE/Application/CliApplication.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Services/ServiceTraits.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
    std::atomic<std::sig_atomic_t> PendingSignal { 0 };
    static_assert(
        decltype(PendingSignal)::is_always_lock_free,
        "CliApplication signal delivery requires lock-free atomics.");

    void HandleProcessSignal(int signalNumber) noexcept
    {
        PendingSignal.store(signalNumber, std::memory_order_relaxed);
    }

#if defined(_WIN32)
    BOOL WINAPI HandleConsoleControl(DWORD controlType) noexcept
    {
        switch (controlType)
        {
        case CTRL_C_EVENT:
            PendingSignal.store(SIGINT, std::memory_order_relaxed);
            return TRUE;
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            PendingSignal.store(SIGTERM, std::memory_order_relaxed);
            return TRUE;
        default:
            return FALSE;
        }
    }
#endif

    std::mutex& SignalRegistrationMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    class SignalRegistration final
    {
    public:
        explicit SignalRegistration(
            std::shared_ptr<TGE::ApplicationLifetime> lifetime)
            : ownership(SignalRegistrationMutex(), std::try_to_lock)
        {
            if (!ownership.owns_lock())
            {
                throw std::logic_error(
                    "Only one CliApplication can own process signal handlers.");
            }

            PendingSignal.store(0, std::memory_order_relaxed);
            previousInterrupt = std::signal(SIGINT, HandleProcessSignal);
            if (previousInterrupt == SIG_ERR)
            {
                throw std::runtime_error("Failed to install the SIGINT handler.");
            }

            previousTerminate = std::signal(SIGTERM, HandleProcessSignal);
            if (previousTerminate == SIG_ERR)
            {
                std::signal(SIGINT, previousInterrupt);
                throw std::runtime_error("Failed to install the SIGTERM handler.");
            }

#if defined(_WIN32)
            if (!SetConsoleCtrlHandler(HandleConsoleControl, TRUE))
            {
                std::signal(SIGTERM, previousTerminate);
                std::signal(SIGINT, previousInterrupt);
                throw std::runtime_error(
                    "Failed to install the Windows console control handler.");
            }
            consoleHandlerInstalled = true;
#endif

            try
            {
                monitor = std::jthread(
                    [lifetime = std::move(lifetime)](std::stop_token stopping)
                    {
                        while (!stopping.stop_requested())
                        {
                            const auto signalNumber = PendingSignal.exchange(
                                0, std::memory_order_relaxed);
                            if (signalNumber != 0)
                            {
                                lifetime->RequestStop(128 + signalNumber);
                                return;
                            }

                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(5));
                        }
                    });
            }
            catch (...)
            {
#if defined(_WIN32)
                SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
                consoleHandlerInstalled = false;
#endif
                std::signal(SIGTERM, previousTerminate);
                std::signal(SIGINT, previousInterrupt);
                throw;
            }
        }

        ~SignalRegistration()
        {
            monitor.request_stop();
            if (monitor.joinable())
            {
                monitor.join();
            }

#if defined(_WIN32)
            if (consoleHandlerInstalled)
            {
                SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
            }
#endif
            std::signal(SIGTERM, previousTerminate);
            std::signal(SIGINT, previousInterrupt);
            PendingSignal.store(0, std::memory_order_relaxed);
        }

        SignalRegistration(const SignalRegistration&) = delete;
        SignalRegistration& operator=(const SignalRegistration&) = delete;
        SignalRegistration(SignalRegistration&&) = delete;
        SignalRegistration& operator=(SignalRegistration&&) = delete;

    private:
        using SignalHandler = void (*)(int);

        std::unique_lock<std::mutex> ownership;
        SignalHandler previousInterrupt = SIG_DFL;
        SignalHandler previousTerminate = SIG_DFL;
#if defined(_WIN32)
        bool consoleHandlerInstalled = false;
#endif
        std::jthread monitor;
    };

    class CliSignalService final : public TGE::IHostedService
    {
    public:
        explicit CliSignalService(
            std::shared_ptr<TGE::ApplicationLifetime> lifetime)
            : lifetime(std::move(lifetime))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            registration = std::make_unique<SignalRegistration>(lifetime);
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            registration.reset();
            co_return;
        }

    private:
        std::shared_ptr<TGE::ApplicationLifetime> lifetime;
        std::unique_ptr<SignalRegistration> registration;
    };
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    CliSignalService,
    TGE::Inject<TGE::ApplicationLifetime>());
#endif

namespace TGE
{
    CliApplication::CliApplication()
        : application(Application::Create())
    {
        application.AddHostedService<CliSignalService>();
    }

    CliApplication::~CliApplication() = default;

    CliApplication CliApplication::Create()
    {
        return CliApplication {};
    }

    ServiceCollection& CliApplication::Services() &
    {
        return application.Services();
    }

    int CliApplication::Run() &
    {
        return application.Run();
    }

    Task<int> CliApplication::RunAsync() &
    {
        return application.RunAsync();
    }

    bool CliApplication::RequestStop(int exitCode) noexcept
    {
        return application.RequestStop(exitCode);
    }

    ApplicationState CliApplication::GetState() const noexcept
    {
        return application.GetState();
    }
}
