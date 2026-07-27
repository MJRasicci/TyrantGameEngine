# Application lifecycle

`TGE::Application` is the concrete composition root and lifetime owner for an
engine process. Applications are configured through service registration rather
than inheritance:

```cpp
#include <TGE/Application.hpp>

auto application = TGE::Application::Create();

application.Services().AddSingleton<IClock, Clock>();
application.AddHostedService<MyTool>();

return application.Run();
```

An `Application` is single-use. Its observable states are `Created`, `Starting`,
`Running`, `Stopping`, `Stopped`, and `Failed`. The service collection is mutable
only while the application remains in `Created`.

## Hosted services

A hosted service implements `TGE::IHostedService`:

```cpp
class MyTool final : public TGE::IHostedService
{
public:
    TGE::Task<void> StartAsync(std::stop_token stopping) override;
    TGE::Task<void> StopAsync() override;
};
```

`StartAsync` completes when startup is ready; it does not represent the
service's entire useful lifetime. The application starts hosted services in
registration order. Once shutdown begins, every successfully started service is
stopped in reverse registration order.

If startup fails, previously started services are still stopped. If a
`StopAsync` operation fails, the application remembers the first failure and
continues stopping the remaining services before propagating it.

Hosted services are singleton registrations. Ordinary DI registrations remain
unique by service type, while any number of distinct hosted-service
implementations can participate in one application.

The hosted-service list belongs to `Application`, not the Core dependency
injection container. This keeps lifecycle orchestration out of Core while
still resolving hosted service dependencies from the completed provider.

## Shutdown

Application shutdown is cooperative. Call `Application::RequestStop` directly,
or inject `TGE::ApplicationLifetime` into a service:

```cpp
class MyTool final : public TGE::IHostedService
{
public:
    explicit MyTool(std::shared_ptr<TGE::ApplicationLifetime> lifetime)
        : lifetime(std::move(lifetime))
    {
    }

    // ...

private:
    std::shared_ptr<TGE::ApplicationLifetime> lifetime;
};
```

The first request wins and records the exit code. Stop requests are thread-safe,
but the requesting thread does not take over lifecycle execution; shutdown
continues on the task's execution scheduler. `ApplicationLifetime::GetStoppingToken`
supplies the token passed to hosted service startup.

## Synchronous and asynchronous entrypoints

`RunAsync()` returns the canonical lazy C++26 task for the complete lifecycle.
It must be driven to completion, and the `Application` must outlive the
operation.

`Run()` is the process-level convenience entrypoint. It blocks the calling
thread with the execution backend's `sync_wait` and returns the requested exit
code.

TGE uses native C++26 `std::execution` when the active standard library provides
the required sender and task facilities. Otherwise, its generated feature
configuration selects the packaged stdexec compatibility backend. Public code
uses `TGE::Task<T>` so the lifecycle API remains the same under either backend.
