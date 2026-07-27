# Tyrant Game Engine 2.0

> **This project is experimental and not yet ready for use.** This is a rewrite/spiritual successor to my first game engine. It is in the very early stages, and some of the documentation may reflect planned future state rather than current completed features. Additionally, public APIs are unstable, things will break, and no binaries have been published yet. Check back later to see progress towards an initial release.

Tyrant Game Engine (TGE) is a modular, data-driven runtime focused on rapid iteration for game prototypes and experimental rendering pipelines. The repository provides the core runtime, supporting tools, and build workflows needed to explore engine concepts across desktop platforms.

## Table of Contents

- [Project Overview](#project-overview)
- [Dependencies](#dependencies)
- [Setup Instructions](#setup-instructions)
  - [Install Prerequisites](#install-prerequisites)
  - [Install Tyrant Game Engine](#install-tyrant-game-engine)
- [Build from Source](#build-from-source)
  - [Using CMake Presets](#using-cmake-presets)
  - [Reflection-generated dependency injection](#reflection-generated-dependency-injection)
  - [Asynchronous execution backend](#asynchronous-execution-backend)
  - [Typed options serialization](#typed-options-serialization)
  - [Running Tests and Benchmarks](#running-tests-and-benchmarks)
  - [Packaging](#packaging)
  - [Workflow Shortcuts](#workflow-shortcuts)
  - [Output Directory Layout](#output-directory-layout)
- [Usage Instructions](#usage-instructions)
- [License](#license)

## Project Overview

TGE emphasizes clean abstractions, testability, and extensible subsystems so new gameplay or rendering features can be layered in without disrupting existing modules. The engine runtime is paired with editor tooling and documentation that demonstrate how to bootstrap game experiences, integrate custom modules, and iterate quickly across platforms.

## Dependencies

Core build requirements:
- **C/C++ Compiler**
  - Windows: Microsoft Visual C++ (MSVC) with C++26/latest mode
  - macOS: Apple Clang with C++26/2c mode
  - Linux: GCC or Clang with C++26/2c mode
- **[CMake](https://cmake.org/)** (3.25+ required)
- **[Ninja](https://ninja-build.org/)**

TGE requires C++26 language mode for every target. Because compiler and
standard-library vendors ship individual C++26 facilities incrementally, CMake
also probes features such as reflection and the execution control library
instead of inferring support from a compiler version alone.

Optional tooling:
- **[Doxygen](https://www.doxygen.nl/)** and **[Graphviz](https://graphviz.org/)** for generating API documentation
- **[GoogleTest](https://github.com/google/googletest)** / **GoogleMock** / **[Google Benchmark](https://github.com/google/benchmark)** for unit tests and benchmarks

[(Back to top)](#table-of-contents)

## Setup Instructions

### Install Prerequisites

#### Linux

Use the Linux setup script to bootstrap the required toolchain along with optional dependencies for generating documentation and running automated tests/benchmarks:

```bash
./.config/linux/setup.sh
```

The script auto-detects Debian/Ubuntu (APT), Fedora/RHEL (DNF), Arch/Manjaro (Pacman), Alpine (APK), OpenSUSE (Zypper), and Gentoo (Emerge) environments. Explore advanced usage with `./.config/linux/setup.sh --help`:

- `-y/--yes` skips the confirmation prompt before installing packages.
- `-r/--required` restricts installs to the minimal build toolchain.
- `-v/--verbose` surfaces package-manager output for troubleshooting.

#### macOS

The macOS setup script targets the system `zsh` shell and handles both Homebrew and Xcode Command Line Tool prerequisites before installing project dependencies:

```bash
./.config/macos/setup.sh
```

Use `./.config/macos/setup.sh --help` to see the same convenience flags for non-interactive or minimal installations.

#### Windows

You can use our winget configuration to automatically download all required tools for Windows (execute in an elevated powershell session):

```powershell
winget configure -f ./.config/windows/configuration.winget
```

### Install Tyrant Game Engine

Pre-compiled binaries are not available yet; follow the build-from-source workflow below to compile the engine locally.

[(Back to top)](#table-of-contents)

## Build from Source

TGE ships with an extensive `CMakePresets.json` that encapsulates all common configure, build, and test workflows. Presets automatically stage outputs in the `artifacts/` directory so runtime assets, libraries, documentation, and packages stay organized.

### Using CMake Presets

Presets follow a `{platform}-{architecture}-{configuration}` convention (e.g., `linux-x64-debug` or `windows-arm64-release`). Each platform preset inherits shared base settings so you can mix and match any combination of:

- **Platform:** `windows`, `macos`, or `linux`
- **Architecture:** `x64` or `arm64`
- **Configuration:** `debug` or `release`

Configure the project for your preferred toolchain:

```bash
cmake --preset linux-x64-debug
```

Build targets for the active preset:

```bash
cmake --build --preset linux-x64-debug
```

### Reflection-generated dependency injection

TGE probes the active compiler for the C++26 reflection language and library
surface used by its service activator. The `TGE_REFLECTION_DI` CMake setting
controls the result:

- `AUTO` (default) enables reflection when the probe succeeds and otherwise
  keeps the portable traits-based activator.
- `ON` requires reflection and stops configuration with a clear error when the
  toolchain cannot provide it.
- `OFF` skips the probe and always uses the portable activator.

On GCC and Clang implementations that require it, the reflected path uses the
`-freflection` compiler option. TGE's CMake targets propagate the selected
compiler options and generated feature configuration to in-tree consumers
automatically.

A service implementation with one public, non-copy, non-move constructor needs
no additional metadata:

```cpp
struct Renderer
{
    Renderer(
        std::shared_ptr<IGpuDevice> device,
        std::shared_ptr<IAssetStore> assets);
};

services.AddTransient<Renderer>();
```

When an implementation has multiple eligible constructors, select exactly one
with the portable annotation macro:

```cpp
struct Renderer
{
    Renderer();

    TGE_INJECT_CONSTRUCTOR
    Renderer(
        std::shared_ptr<IGpuDevice> device,
        std::shared_ptr<IAssetStore> assets);
};
```

Reflection-generated activation currently supports `std::shared_ptr<T>`,
`TGE::ServiceLocator&`, and `TGE::ServiceLocator*` constructor parameters.
The service locator forms are retained for compatibility; ordinary services
should prefer explicit typed dependencies. Builds using the fallback continue
to use `TGE_DECLARE_SERVICE_DEPENDENCIES`.

### Asynchronous execution backend

TGE's application lifecycle follows the C++26 sender/receiver execution model.
The `TGE_EXECUTION_BACKEND` CMake setting selects its implementation:

- `AUTO` (default) uses native `std::execution` when the standard library
  provides senders, `std::execution::task`, and
  `std::this_thread::sync_wait`; otherwise it uses the compatibility backend.
- `NATIVE` requires those native C++26 library facilities and fails
  configuration when they are missing.
- `STDEXEC` forces TGE's pinned NVIDIA stdexec compatibility backend.

The compatibility backend is downloaded during configuration and packaged with
installed builds. Public code uses `TGE::Task<T>` and
`TGE::Execution::SyncWait`, so consumers do not select a backend independently
from the runtime they link.

### Typed options serialization

TGE's typed Options API uses a pinned Glaze serialization backend. Public
aggregates serialize without handwritten metadata on supported GCC, Clang,
Apple Clang, and MSVC toolchains. CMake probes the active C++26 reflection
implementation and enables direct field reflection when the exact integration
compiles; other toolchains use the portable aggregate path.

`TGE_OPTIONS_REFLECTION` independently selects `AUTO` (the default), `ON`, or
`OFF` using the same probe/require/disable semantics as `TGE_REFLECTION_DI`.
This keeps serialization capability guards accurate and makes the portable path
straightforward to exercise in every platform's CI. Public headers also fall
back safely when an installed consumer does not enable the compiler's
reflection mode, even if the packaged runtime was built with it.

The Glaze headers and license are packaged with installed TGE builds because
serialization is part of the public template surface.

### Running Tests and Benchmarks

When GoogleTest/GoogleMock and Google Benchmark are available, enable and execute tests via CTest:

```bash
ctest --preset linux-x64-debug
```

Benchmarks are exposed as CTest entries that forward to the benchmark executables. Use `--preset` in the same way to emit benchmark output.

### Packaging

Packaging presets are provided for **release** configurations only. Attempting to package a debug build (e.g., `linux-x64-debug`) will fail because the corresponding preset does not exist. To generate distributable bundles, target a release preset:

```bash
cmake --build --preset linux-x64-release --target package
```

or leverage the release workflow presets described below to run configure, build, tests, and packaging in a single command.

Installed packages provide a CMake config package and the linkage-independent
`TGE::runtime` target:

```cmake
find_package(TyrantGameEngine CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE TGE::runtime)
```

The target carries TGE's C++26 requirement, public headers, thread dependency,
selected static or shared runtime, and the pinned public template dependencies.
Consumers therefore use the same execution and serialization backends as the
runtime they link.

### Workflow Shortcuts

Workflow presets chain multiple steps together. Debug workflows perform **configure → build → test**, whereas release workflows add a final **package** step:

```bash
cmake --workflow --preset linux-x64-debug     # run configure + build + test
cmake --workflow --preset linux-x64-release   # run configure + build + test + package
```

These workflows are ideal for continuous integration or full local validation.

### Output Directory Layout

All generated assets live under `artifacts/` by default:

```
artifacts/
├── bin/        # Executable outputs (editor, tests, benchmarks)
├── build/      # CMake cache and intermediate build tree
├── docs/       # Doxygen-generated API documentation
├── lib/        # Static or shared libraries produced by the build
└── pack/       # CPack staging area for distributable bundles
```

This structure keeps temporary and distributable assets separate from source, simplifying cleanup and deployment.

[(Back to top)](#table-of-contents)

## Usage Instructions

`TGE::Application` is the concrete composition root. Register ordinary
dependencies and one or more `TGE::IHostedService` implementations, then choose
the blocking or asynchronous entrypoint:

```cpp
#include <TGE/Application.hpp>
#include <TGE/Core.hpp>

class Tool final : public TGE::IHostedService
{
public:
    explicit Tool(std::shared_ptr<TGE::ApplicationLifetime> lifetime)
        : lifetime(std::move(lifetime))
    {
    }

    TGE::Task<void> StartAsync(std::stop_token) override
    {
        // Perform startup or launch long-running work.
        lifetime->RequestStop();
        co_return;
    }

    TGE::Task<void> StopAsync() override
    {
        // Release lifecycle-owned resources.
        co_return;
    }

private:
    std::shared_ptr<TGE::ApplicationLifetime> lifetime;
};

TGE_DECLARE_SERVICE_DEPENDENCIES(
    Tool,
    TGE::Inject<TGE::ApplicationLifetime>());

int main()
{
    auto application = TGE::Application::Create();
    application.AddHostedService<Tool>();
    return application.Run();
}
```

`Run()` blocks the calling thread around the canonical `RunAsync()` operation.
Hosted services start in registration order and stop in reverse order. A
startup failure stops every service that previously started successfully, and
shutdown continues through the remaining services if one stop operation fails.
Applications can request shutdown from any thread through
`Application::RequestStop` or the injected `ApplicationLifetime`; lifecycle
continuations remain on the application task's execution scheduler.

Custom owning aggregates can also be registered as validated, live options:

```cpp
struct ToolOptions
{
    std::string endpoint { "localhost" };
    std::uint32_t workers { 4 };
};

application.Services()
    .AddOptions<ToolOptions>()
    .FromJsonFile("tool.options.json", { .optional = true })
    .FromEnvironment("TGE_TOOL")
    .Validate(
        [](const ToolOptions& value) { return value.workers > 0; },
        "workers must be positive");
```

Consumers inject `std::shared_ptr<TGE::IOptionsMonitor<ToolOptions>>` and call
`Current()` for an immutable snapshot. Reloads are transactional: invalid
provider data or validation failures preserve the last-known-good value.
See [Typed live options](Docs/Pages/Options.md) for provider extension,
monitoring, serialization, and environment mapping.

After building documentation presets, open `artifacts/docs/html/index.html` for
the generated API reference.

[(Back to top)](#table-of-contents)

## License

Tyrant Game Engine 2.0 is distributed under the [MIT License](LICENSE.md). Refer to the license file for complete terms and conditions.

[(Back to top)](#table-of-contents)
