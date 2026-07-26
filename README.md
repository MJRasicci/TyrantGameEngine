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
  - Windows: Microsoft Visual C++ (MSVC)
  - macOS: Clang (via Xcode Command Line Tools)
  - Linux: GCC or Clang
- **[CMake](https://cmake.org/)** (3.25+ required)
- **[Ninja](https://ninja-build.org/)**

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

After building documentation presets, open `artifacts/docs/html/index.html` for full engine usage instructions, API references, and tutorials.

[(Back to top)](#table-of-contents)

## License

Tyrant Game Engine 2.0 is distributed under the [MIT License](LICENSE.md). Refer to the license file for complete terms and conditions.

[(Back to top)](#table-of-contents)
