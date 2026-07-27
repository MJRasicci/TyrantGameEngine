# Typed live options

TGE Options turns ordinary owning C++ value types into validated, composable,
live configuration. It is designed around four boundaries:

- option types contain data and defaults;
- providers obtain partial configuration from external sources;
- monitors atomically publish immutable snapshots to consumers.
- stores are explicitly granted authority to persist one complete option type.

Options does not depend on logging or application hosting. This keeps it usable
while the dependency injection container and logging services themselves are
being composed.

## Define and register options

Public owning aggregates work without serialization metadata:

```cpp
struct GameplayOptions
{
    std::string player_name { "Player" };
    std::uint32_t worker_count { 4 };
    bool cheats_enabled { false };
};

auto application = TGE::Application::Create();

application.Services()
    .AddOptions<GameplayOptions>()
    .FromJsonFile(
        "gameplay.options.json",
        {
            .optional = true,
            .reloadOnChange = true
        })
    .FromEnvironment("TGE_GAMEPLAY")
    .Validate(
        [](const GameplayOptions& options)
        {
            return options.worker_count > 0;
        },
        "worker_count must be positive");
```

The first `AddOptions<T>` establishes `T`'s defaults. Repeated calls for the
same type reopen the existing builder, allowing separate modules to contribute
providers, programmatic configuration, or validation without creating multiple
monitors.

Option types must be default-initializable and copyable. Use owning fields such
as `std::string`, containers, optionals, nested aggregates, numeric values, and
enums. Do not store references, raw pointers, `std::string_view`, or spans:
provider input buffers are temporary, while published snapshots may live
indefinitely.

## Consume immutable snapshots

Ordinary services depend on the read-only monitor interface:

```cpp
class GameplaySystem
{
public:
    explicit GameplaySystem(
        std::shared_ptr<TGE::IOptionsMonitor<GameplayOptions>> options)
        : options(std::move(options))
    {
    }

    void Tick()
    {
        const auto snapshot = options->Current();
        // snapshot remains valid even if a newer value is published.
        RunWorkers(snapshot->worker_count);
    }

private:
    std::shared_ptr<TGE::IOptionsMonitor<GameplayOptions>> options;
};
```

`Current()` performs one atomic shared-pointer load and returns an owning
`std::shared_ptr<const T>`. No consumer-visible object is mutated in place, so
readers cannot observe a half-applied reload or torn cross-field invariant.
The same singleton monitor is resolved from the root provider and every scope.
When diagnostics also need the matching publication number,
`CurrentSnapshot()` returns the value and version from one atomic state load.

## Source order and partial configuration

Every source is applied to the same private candidate in registration order.
Later sources win only for fields they provide:

1. defaults from `T`;
2. JSON file;
3. environment variables;
4. custom providers or `Configure` steps registered later.

Arrays and complete objects supplied by a later source replace the corresponding
value. Missing fields preserve the value established by an earlier source.
Validation runs after every source has completed.

`Configure` participates in source ordering:

```cpp
services.AddOptions<GameplayOptions>()
    .FromJsonFile("gameplay.options.json", { .optional = true })
    .Configure(
        [](GameplayOptions& options)
        {
            // This override runs after the file because it appears later.
            options.cheats_enabled = false;
        });
```

## Writable stores and trust boundaries

Reading configuration and writing it are separate capabilities.
`IOptionsProvider<T>` applies a source, while `IOptionsStore<T>` persists a
complete `T` to one specific source. Environment providers never implement the
store interface. `FromJsonFile` is also read-only from IOC's perspective:
although the concrete JSON provider knows how to save, the convenience method
does not retain or register that writable capability.

Code must deliberately retain a JSON provider and expose only its store
interface when runtime persistence is appropriate:

```cpp
auto userSettingsFile =
    std::make_shared<TGE::JsonFileOptionsProvider<UserSettings>>(
        "user-settings.json",
        TGE::JsonFileOptionsProviderSettings {
            .optional = true,
            .reloadOnChange = true
        });

services.AddOptions<UserSettings>()
    .AddProvider(userSettingsFile)
    .Validate(IsValidUserSettings, "The user settings are invalid.");

services.AddSingleton<TGE::IOptionsStore<UserSettings>>(
    userSettingsFile);
```

The explicit singleton registration is the write-authority boundary. A
settings controller can inject both the concrete monitor authority and the
specific persistent store, while ordinary consumers continue to inject only
`IOptionsMonitor<UserSettings>`:

```cpp
class UserSettingsController
{
public:
    UserSettingsController(
        std::shared_ptr<TGE::OptionsMonitor<UserSettings>> options,
        std::shared_ptr<TGE::IOptionsStore<UserSettings>> store)
        : options(std::move(options)),
          store(std::move(store))
    {
    }

    TGE::OptionsResult<void> Save(UserSettings next)
    {
        // Set runs the normal validators and updates every live consumer.
        auto published = options->Set(next);
        if (!published)
        {
            return std::unexpected(std::move(published.error()));
        }

        // Save persists exactly the validated value to this one store.
        return store->Save(next);
    }

private:
    std::shared_ptr<TGE::OptionsMonitor<UserSettings>> options;
    std::shared_ptr<TGE::IOptionsStore<UserSettings>> store;
};
```

The monitor publication and external write cannot form one transaction. If
`Save` fails after `Set` succeeds, consumers hold a valid live-only value and
the UI should report the persistence failure or offer a retry. Conversely,
calling `Save` directly does not validate or publish. With
`reloadOnChange = true`, a successful JSON save is observed asynchronously and
passes through every provider and validator before publication. Without file
watching, call `Reload()` explicitly when the saved source should be reapplied.

`Save` does not alter precedence. A later environment, command-line, or remote
provider still wins on reload. Because JSON stores write a full document,
saving a merged monitor snapshot can accidentally copy a higher-priority
override into the lower-priority file. Prefer a dedicated option type for each
trust and mutability boundary: for example, `UserSettings` can be writable,
while `ServerEndpointOptions` can compose a packaging-managed JSON file and
read-only environment overrides. A `server.ini`-style JSON source remains
read-only simply by using `FromJsonFile` without registering its store.

The IOC container permits one `IOptionsStore<T>` registration for each `T`,
making the mutable persistent owner unambiguous. Use distinct option types when
two stores have different owners or security policies. Keyed stores can be
introduced later if a real use case requires multiple writable layers for the
same type.

JSON `Save` is synchronous. It serializes before touching the destination,
writes and flushes an exclusive sibling temporary file, then atomically replaces
the fixed path captured by the provider. Any failure before replacement leaves
the previous document intact and attempts to clean up the temporary file.
Windows replacement uses a sibling recovery backup; if the operating system
reports a partially completed replacement and automatic recovery also fails,
`Save` returns an error and retains both recovery files instead of deleting the
only copies. The store does not create parent directories. Concurrent saves
through one provider are serialized and use last-writer-wins semantics. Atomic
visibility is provided by normal local-filesystem rename/replace behavior;
network filesystems may provide weaker guarantees. Existing Windows ACLs and
POSIX permission bits are preserved when replacing a regular file; a new POSIX
settings file starts owner-only.

## JSON serialization

The same strict serialization API is available independently of providers:

```cpp
GameplayOptions options;

auto json = TGE::SerializeOptions(options, true);
auto copy = TGE::DeserializeOptions<GameplayOptions>(*json);

auto patch = TGE::DeserializeOptionsInto(
    options,
    R"({"worker_count":8})");
```

Deserialization rejects malformed input, unknown keys, incompatible types, and
numeric overflow. `DeserializeOptionsInto` is intentionally partial; absent
fields retain their existing values. Errors use `TGE::OptionsResult<T>` and do
not include raw source values, which avoids exposing environment secrets in
diagnostics.

TGE uses its pinned Glaze backend for fast, RFC-compliant JSON. An exact feature
probe enables direct C++26 field reflection when the active compiler supports
the required operations. Toolchains that do not pass that probe use Glaze's
portable aggregate reflection path with the same TGE API. The independent
`TGE_OPTIONS_REFLECTION` CMake setting accepts `AUTO`, `ON`, or `OFF`, allowing
CI to require the reflected path or deliberately exercise the portable one.

Encapsulated classes can specialize `TGE::OptionsSerializer<T>` while keeping
the monitor and provider APIs unchanged. A small public serialization DTO keeps
that customization explicit and portable:

```cpp
class StorageOptions
{
public:
    StorageOptions() = default;
    StorageOptions(std::string path, int retries);

    const std::string& Path() const noexcept;
    int Retries() const noexcept;

private:
    std::string path { "content" };
    int retries { 3 };
};

struct StorageOptionsDocument
{
    std::string path;
    int retries;
};

template<>
struct TGE::OptionsSerializer<StorageOptions>
{
    static TGE::OptionsResult<std::string> Serialize(
        const StorageOptions& value,
        bool pretty)
    {
        return TGE::SerializeOptions(
            StorageOptionsDocument {
                .path = value.Path(),
                .retries = value.Retries()
            },
            pretty);
    }

    static TGE::OptionsResult<void> DeserializeInto(
        StorageOptions& value,
        std::string_view serialized)
    {
        StorageOptionsDocument candidate {
            .path = value.Path(),
            .retries = value.Retries()
        };
        auto result =
            TGE::DeserializeOptionsInto(candidate, serialized);
        if (result)
        {
            value = StorageOptions(
                std::move(candidate.path),
                candidate.retries);
        }
        return result;
    }
};
```

Custom deserializers should stage changes and assign the target only after a
successful parse, preserving the same transactional behavior as built-in
providers.

## Environment variables

`FromEnvironment("TGE_GAMEPLAY")` uses `__` as the default nesting delimiter:

```text
TGE_GAMEPLAY__PLAYER_NAME=Alex
TGE_GAMEPLAY__WORKER_COUNT=8
TGE_GAMEPLAY__RENDERING__VSYNC=false
```

Environment path segments are lowercased by default, producing
`player_name`, `worker_count`, and `rendering.vsync`. Valid JSON literals retain
their type: `false`, `8`, arrays, objects, and quoted strings are parsed as
JSON. Other values are safely encoded as strings. Pass
`EnvironmentOptionsProviderSettings` to preserve key case or select a different
delimiter.

Environment variables follow the host platform's name-comparison behavior:
case-sensitive on POSIX and case-insensitive on Windows. Environment sources do
not poll automatically; call `Reload()` after changing the process environment.
On POSIX and macOS, process-environment mutation must not overlap `Reload()`
because the platform exposes a mutable global environment array. Serialize
`setenv`/`unsetenv` calls with reloads at the application boundary.

## Monitor changes and update centrally

Caching consumers should initialize and update their state through one ordered
callback path, then keep the token for as long as notifications are required:

```cpp
auto subscription = options->Observe(
    [](const TGE::OptionsChange<GameplayOptions>& change)
    {
        if (!change.previous)
        {
            // The initial value is delivered synchronously by Observe.
            InitializeWorkers(change.current);
            return;
        }

        std::println(
            "Options version {} now uses {} workers",
            change.version,
            change.current->worker_count);
    });
```

`Observe()` delivers the current snapshot synchronously before returning; that
first change has an empty `previous` pointer. Publications racing with
initialization are buffered, then delivered in version order before normal
observation continues. `OnChange()` is available when the caller intentionally
needs only future updates.

Callbacks run outside monitor locks and may call `Current()` safely. An
exception from one callback is isolated and does not prevent later observers
from running. Notifications remain ordered when a callback publishes another
update. Destroying or resetting the `OptionsSubscription` removes the callback
and waits for an in-flight invocation to finish, except when a callback resets
its own token.

Options notifications represent state rather than an event log. If writers
publish faster than observers can run, TGE coalesces not-yet-dispatched changes
to the newest snapshot. Versions remain monotonic, but a callback may observe a
gap and should always consume the value carried by its change object.

Code that owns configuration authority can resolve or retain the concrete
`OptionsMonitor<T>`:

```cpp
auto result = monitor->Update(
    [](GameplayOptions& options)
    {
        options.worker_count = 12;
    });

auto reloaded = monitor->Reload();
```

`Set` and `Update` affect the live value. `Reload` intentionally reconstructs
from defaults and registered sources, so runtime overrides that must survive
reload belong in an observable provider such as `MemoryOptionsProvider<T>`.
Its `Set` and `Clear` operations return whether every attached monitor accepted
the requested reload.

Distinct values increment a monotonically increasing version. Publishing an
equivalent serialized value is deduplicated and does not notify observers.

## Failure behavior

Reload and update are transactional:

1. construct a private candidate;
2. apply every source;
3. run every validator;
4. serialize the canonical value;
5. publish once.

Any provider, parsing, update, or validation failure discards the candidate.
Consumers retain the last-known-good snapshot and receive no change event.
`LastError()` exposes the most recent failure and is cleared by the next
successful operation.

Fluent builder calls perform an immediate reload and throw
`TGE::OptionsException` when initial application configuration is invalid.
The failed builder step is not retained, so callers may correct the
configuration and continue using the same builder and monitor.
Runtime `Reload`, `Set`, and `Update` return `OptionsResult<std::uint64_t>`
instead, allowing a running process to decide how to report or recover.

## Custom providers

Any source can implement the typed provider interface. A future database-backed
provider can assign fields directly or reuse strict JSON patches:

```cpp
class DatabaseGameplayOptions final
    : public TGE::IOptionsProvider<GameplayOptions>
{
public:
    explicit DatabaseGameplayOptions(std::shared_ptr<ConfigurationDb> database)
        : database(std::move(database))
    {
    }

    std::string_view Name() const noexcept override
    {
        return "gameplay configuration database";
    }

    TGE::OptionsResult<void> Apply(GameplayOptions& candidate) const override
    {
        auto document = database->Read("gameplay");
        if (!document)
        {
            return std::unexpected(TGE::OptionsError {
                .code = TGE::OptionsErrorCode::Provider,
                .source = std::string(Name()),
                .message = "The database query failed."
            });
        }

        return TGE::DeserializeOptionsInto(candidate, *document, Name());
    }

private:
    std::shared_ptr<ConfigurationDb> database;
};

services.AddOptions<GameplayOptions>()
    .AddProvider(std::make_shared<DatabaseGameplayOptions>(database));
```

Providers that can observe external changes override `Watch` and return an
RAII `OptionsSubscription`. Calling the supplied callback asks the monitor to
run a complete transactional reload, preserving normal source precedence and
validation. The callback returns `true` only when the reload was accepted.
Providers that track an external revision or file signature should retain and
retry a rejected change until a later callback succeeds.

The monitor installs `Watch` before the provider's initial `Apply`, closing the
usual subscribe/read handoff race. A provider should make its notification
callback visible before `Watch` returns. Changes reported while registration is
still applying are coalesced into a reload immediately after that provider is
published.

## Logging and options

Options and logging are complementary but independent architectural patterns.
Serializable logging values such as level or format can eventually use a typed
monitor. Polymorphic runtime resources such as `ILogSink` implementations
remain service registrations and should not be serialized into an options DTO.
This separation keeps configuration data, service composition, and logging I/O
independently testable.

The Editor target provides a compiled end-to-end example combining application
lifecycle, dependency injection, logging, JSON/environment options, validation,
change monitoring, and explicit JSON store injection. Starting the Editor does
not write the file; a future settings UI calls the injected store only when the
user chooses to persist a validated change.
