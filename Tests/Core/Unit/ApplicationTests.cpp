#include <gtest/gtest.h>

#include "TGE/Application/Application.hpp"
#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Logging/ILogDispatcher.hpp"
#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceTraits.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#if TGE_HAS_NATIVE_STD_EXECUTION
namespace TGEExecutionBackend = std::execution;
#else
namespace TGEExecutionBackend = stdexec;
#endif

namespace
{
    class LifecycleRecord
    {
    public:
        void Add(std::string event)
        {
            std::scoped_lock lock(mutex);
            events.emplace_back(std::move(event));
        }

        std::vector<std::string> Snapshot() const
        {
            std::scoped_lock lock(mutex);
            return events;
        }

        void RecordStartThread()
        {
            std::scoped_lock lock(mutex);
            startThread = std::this_thread::get_id();
        }

        void RecordStopThread()
        {
            std::scoped_lock lock(mutex);
            stopThread = std::this_thread::get_id();
        }

        std::thread::id StartThread() const
        {
            std::scoped_lock lock(mutex);
            return startThread;
        }

        std::thread::id StopThread() const
        {
            std::scoped_lock lock(mutex);
            return stopThread;
        }

    private:
        mutable std::mutex mutex;
        std::vector<std::string> events;
        std::thread::id startThread;
        std::thread::id stopThread;
    };

    class FirstHostedService final : public TGE::IHostedService
    {
    public:
        explicit FirstHostedService(std::shared_ptr<LifecycleRecord> record)
            : record(std::move(record))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->Add("start:first");
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            record->Add("stop:first");
            co_return;
        }

    private:
        std::shared_ptr<LifecycleRecord> record;
    };

    class StoppingHostedService final : public TGE::IHostedService
    {
    public:
        StoppingHostedService(
            std::shared_ptr<LifecycleRecord> record,
            std::shared_ptr<TGE::ApplicationLifetime> lifetime)
            : record(std::move(record)),
              lifetime(std::move(lifetime))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->Add("start:stopping");
            lifetime->RequestStop(7);
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            record->Add("stop:stopping");
            co_return;
        }

    private:
        std::shared_ptr<LifecycleRecord> record;
        std::shared_ptr<TGE::ApplicationLifetime> lifetime;
    };

    class FailingHostedService final : public TGE::IHostedService
    {
    public:
        explicit FailingHostedService(std::shared_ptr<LifecycleRecord> record)
            : record(std::move(record))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->Add("start:failing");
            throw std::runtime_error("startup failed");
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            record->Add("stop:failing");
            co_return;
        }

    private:
        std::shared_ptr<LifecycleRecord> record;
    };

    class FailingStopService final : public TGE::IHostedService
    {
    public:
        explicit FailingStopService(std::shared_ptr<LifecycleRecord> record)
            : record(std::move(record))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->Add("start:failing-stop");
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            record->Add("stop:failing-stop");
            throw std::runtime_error("shutdown failed");
            co_return;
        }

    private:
        std::shared_ptr<LifecycleRecord> record;
    };

    class CancelledStartService final : public TGE::IHostedService
    {
    public:
        explicit CancelledStartService(std::shared_ptr<LifecycleRecord> record)
            : record(std::move(record))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->Add("start:cancelled");
            co_await TGEExecutionBackend::just_stopped();
        }

        TGE::Task<void> StopAsync() override
        {
            record->Add("stop:cancelled");
            co_return;
        }

    private:
        std::shared_ptr<LifecycleRecord> record;
    };

    class CancelledStopService final : public TGE::IHostedService
    {
    public:
        explicit CancelledStopService(std::shared_ptr<LifecycleRecord> record)
            : record(std::move(record))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->Add("start:cancelled-stop");
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            record->Add("stop:cancelled-stop");
            co_await TGEExecutionBackend::just_stopped();
        }

    private:
        std::shared_ptr<LifecycleRecord> record;
    };

    class StartSignal
    {
    public:
        void Signal()
        {
            {
                std::scoped_lock lock(mutex);
                started = true;
            }
            condition.notify_all();
        }

        void Wait()
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [this] { return started; });
        }

    private:
        std::mutex mutex;
        std::condition_variable condition;
        bool started = false;
    };

    class WaitingHostedService final : public TGE::IHostedService
    {
    public:
        WaitingHostedService(
            std::shared_ptr<LifecycleRecord> record,
            std::shared_ptr<StartSignal> signal)
            : record(std::move(record)),
              signal(std::move(signal))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            record->RecordStartThread();
            record->Add("start:waiting");
            signal->Signal();
            co_return;
        }

        TGE::Task<void> StopAsync() override
        {
            record->RecordStopThread();
            record->Add("stop:waiting");
            co_return;
        }

    private:
        std::shared_ptr<LifecycleRecord> record;
        std::shared_ptr<StartSignal> signal;
    };

    class NoopDispatcher final : public TGE::ILogDispatcher
    {
    public:
        void Log(const TGE::LogMessage&) override
        {
        }

        void Flush() override
        {
        }
    };

    class ThrowingStopDispatcher final : public TGE::ILogDispatcher
    {
    public:
        void Log(const TGE::LogMessage& message) override
        {
            if (message.Body == "Stopping application")
            {
                throw std::runtime_error("stopping log failed");
            }
        }

        void Flush() override
        {
            flushed = true;
        }

        bool flushed = false;
    };
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    FirstHostedService,
    TGE::Inject<LifecycleRecord>());
TGE_DECLARE_SERVICE_DEPENDENCIES(
    StoppingHostedService,
    TGE::Inject<LifecycleRecord>(),
    TGE::Inject<TGE::ApplicationLifetime>());
TGE_DECLARE_SERVICE_DEPENDENCIES(
    FailingHostedService,
    TGE::Inject<LifecycleRecord>());
TGE_DECLARE_SERVICE_DEPENDENCIES(
    FailingStopService,
    TGE::Inject<LifecycleRecord>());
TGE_DECLARE_SERVICE_DEPENDENCIES(
    CancelledStartService,
    TGE::Inject<LifecycleRecord>());
TGE_DECLARE_SERVICE_DEPENDENCIES(
    CancelledStopService,
    TGE::Inject<LifecycleRecord>());
TGE_DECLARE_SERVICE_DEPENDENCIES(
    WaitingHostedService,
    TGE::Inject<LifecycleRecord>(),
    TGE::Inject<StartSignal>());
#endif

TEST(ApplicationTests, RunsHostedServicesInLifecycleOrder)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton(record);
    application.Services().AddHostedService<FirstHostedService>();
    application.Services().AddHostedService<StoppingHostedService>();

    EXPECT_EQ(application.Run(), 7);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Stopped);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "start:first",
            "start:stopping",
            "stop:stopping",
            "stop:first"
        }));
}

TEST(ApplicationTests, ExposesComposableAsynchronousEntrypoint)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton(record);
    application.Services().AddHostedService<StoppingHostedService>();

    auto result = TGE::Execution::SyncWait(application.RunAsync());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 7);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Stopped);
}

TEST(ApplicationTests, StopsStartedServicesWhenStartupFails)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton(record);
    application.Services().AddHostedService<FirstHostedService>();
    application.Services().AddHostedService<FailingHostedService>();

    EXPECT_THROW(application.Run(), std::runtime_error);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Failed);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "start:first",
            "start:failing",
            "stop:first"
        }));
}

TEST(ApplicationTests, StopsStartedServicesWhenStartupIsCancelled)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton(record);
    application.Services().AddHostedService<FirstHostedService>();
    application.Services().AddHostedService<CancelledStartService>();

    EXPECT_THROW(application.Run(), std::runtime_error);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Failed);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "start:first",
            "start:cancelled",
            "stop:first"
        }));
}

TEST(ApplicationTests, WaitsForStopRequestFromAnotherThread)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();
    auto signal = std::make_shared<StartSignal>();
    const auto executionThread = std::this_thread::get_id();

    application.Services().AddSingleton(record);
    application.Services().AddSingleton(signal);
    application.Services().AddHostedService<WaitingHostedService>();

    std::jthread requester([&]
    {
        signal->Wait();

        while (application.GetState() != TGE::ApplicationState::Running)
        {
            std::this_thread::yield();
        }

        application.RequestStop(3);
    });

    EXPECT_EQ(application.Run(), 3);
    EXPECT_EQ(record->StartThread(), executionThread);
    EXPECT_EQ(record->StopThread(), executionThread);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "start:waiting",
            "stop:waiting"
        }));
}

TEST(ApplicationTests, ContinuesStoppingServicesAfterShutdownFailure)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton(record);
    application.Services().AddHostedService<FirstHostedService>();
    application.Services().AddHostedService<FailingStopService>();
    application.Services().AddHostedService<StoppingHostedService>();

    EXPECT_THROW(application.Run(), std::runtime_error);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Failed);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "start:first",
            "start:failing-stop",
            "start:stopping",
            "stop:stopping",
            "stop:failing-stop",
            "stop:first"
        }));
}

TEST(ApplicationTests, ContinuesStoppingServicesAfterShutdownCancellation)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton(record);
    application.Services().AddHostedService<FirstHostedService>();
    application.Services().AddHostedService<CancelledStopService>();
    application.Services().AddHostedService<StoppingHostedService>();

    EXPECT_THROW(application.Run(), std::runtime_error);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Failed);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "start:first",
            "start:cancelled-stop",
            "start:stopping",
            "stop:stopping",
            "stop:cancelled-stop",
            "stop:first"
        }));
}

TEST(ApplicationTests, ContinuesShutdownWhenStoppingLogThrows)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();
    auto dispatcher = std::make_shared<ThrowingStopDispatcher>();

    application.Services().AddSingleton<TGE::ILogDispatcher>(dispatcher);
    application.Services().AddSingleton(record);
    application.Services().AddHostedService<FirstHostedService>();
    application.Services().AddHostedService<StoppingHostedService>();

    EXPECT_THROW(application.Run(), std::runtime_error);
    EXPECT_EQ(application.GetState(), TGE::ApplicationState::Failed);
    EXPECT_TRUE(dispatcher->flushed);
    EXPECT_EQ(
        record->Snapshot(),
        (std::vector<std::string> {
            "start:first",
            "start:stopping",
            "stop:stopping",
            "stop:first"
        }));
}

TEST(ApplicationTests, ConsumerRegistrationReplacesDefaultLogging)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton<TGE::ILogDispatcher, NoopDispatcher>();
    application.Services().AddSingleton(record);
    application.Services().AddHostedService<StoppingHostedService>();

    EXPECT_EQ(application.Run(), 7);
}

TEST(ApplicationTests, ApplicationInstancesAreSingleUse)
{
    auto application = TGE::Application::Create();
    auto record = std::make_shared<LifecycleRecord>();

    application.Services().AddSingleton(record);
    application.Services().AddHostedService<StoppingHostedService>();

    ASSERT_EQ(application.Run(), 7);
    EXPECT_THROW(application.Run(), std::logic_error);
    EXPECT_THROW(application.Services(), std::logic_error);
}
