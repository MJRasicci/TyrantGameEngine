#include <gtest/gtest.h>

#include "TGE/Application/CliApplication.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Services/ServiceTraits.hpp"

#include <atomic>
#include <csignal>
#include <memory>

namespace
{
    std::atomic<std::sig_atomic_t> RestoredSignal { 0 };
    static_assert(decltype(RestoredSignal)::is_always_lock_free);

    void RecordSignal(int signalNumber) noexcept
    {
        RestoredSignal.store(signalNumber, std::memory_order_relaxed);
    }

    class SignalHandlerRestorer final
    {
    public:
        using SignalHandler = void (*)(int);

        SignalHandlerRestorer(int signalNumber, SignalHandler handler)
            : signalNumber(signalNumber),
              handler(handler)
        {
        }

        ~SignalHandlerRestorer()
        {
            if (handler != SIG_ERR)
            {
                std::signal(signalNumber, handler);
            }
        }

        SignalHandlerRestorer(const SignalHandlerRestorer&) = delete;
        SignalHandlerRestorer& operator=(const SignalHandlerRestorer&) = delete;

    private:
        int signalNumber;
        SignalHandler handler;
    };

    class SignalLifecycleRecord final
    {
    public:
        std::atomic<bool> started { false };
        std::atomic<bool> stopped { false };
    };

    class InterruptingService final : public TGE::IHostedService
    {
    public:
        explicit InterruptingService(
            std::shared_ptr<SignalLifecycleRecord> record)
            : record(std::move(record))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->started.store(true);
            std::raise(SIGINT);
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            record->stopped.store(true);
            co_return;
        }

    private:
        std::shared_ptr<SignalLifecycleRecord> record;
    };
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    InterruptingService,
    TGE::Inject<SignalLifecycleRecord>());
#endif

TEST(CliApplicationTests, ConvertsInterruptIntoCooperativeShutdown)
{
    RestoredSignal.store(0, std::memory_order_relaxed);
    const auto previousInterrupt = std::signal(SIGINT, RecordSignal);
    SignalHandlerRestorer restoreInterrupt(SIGINT, previousInterrupt);
    ASSERT_NE(previousInterrupt, SIG_ERR);

    auto application = TGE::CliApplication::Create();
    auto record = std::make_shared<SignalLifecycleRecord>();

    application.Services().AddSingleton(record);
    application.AddHostedService<InterruptingService>();

    EXPECT_EQ(application.Run(), 128 + SIGINT);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Stopped);
    EXPECT_TRUE(record->started.load());
    EXPECT_TRUE(record->stopped.load());
    EXPECT_EQ(RestoredSignal.load(std::memory_order_relaxed), 0);

    ASSERT_EQ(std::raise(SIGINT), 0);
    EXPECT_EQ(
        RestoredSignal.load(std::memory_order_relaxed),
        SIGINT);
}
