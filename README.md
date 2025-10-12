# Tyrant Game Engine 2.0

Tyrant Game Engine (TGE) is a modular, data-driven runtime focused on rapid iteration for game prototypes and experimental rendering pipelines. The repository provides the core runtime, supporting tools, and build workflows needed to explore engine concepts across desktop platforms.

## Table of Contents
- [Project Overview](#project-overview)
- [Dependencies](#dependencies)
- [Setup Instructions](#setup-instructions)
  - [Install Prerequisites](#install-prerequisites)
  - [Configure Environment Variables](#configure-environment-variables)
- [Build from Source](#build-from-source)
  - [Using CMake Presets](#using-cmake-presets)
  - [Running Tests and Benchmarks](#running-tests-and-benchmarks)
  - [Packaging](#packaging)
  - [Workflow Shortcuts](#workflow-shortcuts)
  - [Output Directory Layout](#output-directory-layout)
- [Usage Instructions](#usage-instructions)

## Project Overview
TGE emphasizes clean abstractions, testability, and extensible subsystems so new gameplay or rendering features can be layered in without disrupting existing modules. The engine runtime is paired with editor tooling and documentation that demonstrate how to bootstrap game experiences, integrate custom modules, and iterate quickly across platforms.

[(Back to top)](#table-of-contents)

## Dependencies
Core build requirements:
- **C/C++ Compiler**
  - Windows: Microsoft Visual C++ (MSVC)
  - macOS: Clang (via Xcode Command Line Tools)
  - Linux: GCC or Clang
- **[CMake](https://cmake.org/)** (3.27+ recommended)
- **[Ninja](https://ninja-build.org/)**

Optional tooling:
- **[Doxygen](https://www.doxygen.nl/)** and **[Graphviz](https://graphviz.org/)** for generating API documentation
- **[GoogleTest](https://github.com/google/googletest)** / **GoogleMock** / **[Google Benchmark](https://github.com/google/benchmark)** for unit tests and benchmarks

[(Back to top)](#table-of-contents)

## Setup Instructions
### Install Prerequisites
<details>
<summary><b>Ubuntu / Debian</b></summary>

```bash
sudo apt update
sudo apt install build-essential clang ninja-build cmake doxygen graphviz libgtest-dev libbenchmark-dev
```

> **Note:** `libgtest-dev` and `libbenchmark-dev` provide the headers and libraries required for unit tests and benchmarks. If they are unavailable, you can still build the engine without the test targets.

</details>

<details>
<summary><b>Fedora / Red Hat</b></summary>

```bash
sudo dnf install gcc gcc-c++ clang ninja-build cmake doxygen graphviz gtest-devel benchmark-devel
```

> **Note:** Some distributions package Google Benchmark separately (e.g., `benchmark` vs. `benchmark-devel`). Install the development package for headers and libraries.

</details>

Pre-compiled binaries are not available yet; follow the build-from-source workflow below to compile the engine locally.

### Configure Environment Variables
No special environment variables are required. Ensure your compiler, CMake, and Ninja are discoverable on the system `PATH` before configuring the project.

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
cmake --preset linux-x64-debug    # or linux-x64-release, windows-msvc-release, etc.
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
cmake --workflow --preset linux-x64-debug     # configure + build + test
cmake --workflow --preset linux-x64-release   # configure + build + test + package
```

These workflows are ideal for continuous integration or full local validation.

### Output Directory Layout
All generated assets live under `artifacts/` by default:

```
artifacts/
├── build/      # CMake cache and intermediate build tree
├── bin/        # Executable outputs (engine runtime, tools, tests, benchmarks)
├── lib/        # Static or shared libraries produced by the build
├── docs/       # Doxygen-generated API documentation (HTML, XML, etc.)
└── packages/   # CPack staging area for distributable bundles
```

This structure keeps temporary and distributable assets separate from source, simplifying cleanup and deployment.

[(Back to top)](#table-of-contents)

## Usage Instructions
After building documentation presets, open `artifacts/docs/html/index.html` for full engine usage instructions, API references, and tutorials.

[(Back to top)](#table-of-contents)
