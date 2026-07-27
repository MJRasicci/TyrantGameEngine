# Windowing backends

Tyrant owns the public windowing model. Platform adapters provide operating
system mechanics behind that model without defining its lifecycle, threading,
or rendering semantics.

## Current desktop backend

`SDLDesktop` is Tyrant's first working desktop backend. It privately uses the
pinned SDL 3.4.12 shared library to provide:

- desktop window creation, mutation, destruction, and event translation;
- logical window geometry, framebuffer dimensions, and display scale;
- keyboard, text, pointer, touch, and device event translation for Input; and
- one wakeable desktop event queue shared by the window and input adapters.

SDL is an implementation aid, not the specification for either public API.
Graphics and Input contracts, together with their fake-platform tests, remain
the source of truth.

The dependency boundary is:

```text
GuiApplication
  -> IWindowManager + IInputManager + IWindowInputContextFactory
  -> WindowManager + InputManager
  -> one externally driven DesktopEventRuntime
  -> private SDL window adapter + private SDL input adapter
  -> SDL 3.4.12 shared library
```

`SDLDesktop`, `DesktopEventRuntime`, and the Graphics and Input platform SPIs
are build-tree-only implementation modules. SDL headers, types, pointers,
flags, identifiers, property bags, and errors do not enter public Tyrant
headers or installed usage requirements.

## GUI application composition

`GuiApplication::UseDefaultDesktopBackend()` selects the backend built into the
runtime. When `SDLDesktop` is enabled, it creates one shared backend state and
registers only the following public abstractions with dependency injection:

- `IWindowManager`;
- `IInputManager`; and
- `IWindowInputContextFactory`.

`ConfigureRootWindow()` remains separate from backend selection. During
application startup, the GUI coordinator creates the root window and asks the
window/input bridge for a context routed to that engine window. The resulting
`WindowSession` exposes both `Window()` and `InputContext()`. Closing the root
ends its window-owned service scope and requests application shutdown; SDL
never owns that policy.

Applications may instead register another implementation of the same public
abstractions. `UseDefaultDesktopBackend()` refuses to replace existing window,
input, or bridge registrations, and it reports an error when the runtime was
built without a default desktop backend.

## Threading and event-loop ownership

`IWindowManager` and `IInputManager` are thread-safe facades. Callers may query
state, subscribe, or initiate asynchronous mutations from arbitrary threads
without inheriting SDL's video-thread affinity rules.

`DesktopEventRuntime` deliberately owns no thread. `GuiApplication::Run()`
drives it on the calling thread while the generic application lifecycle runs
on a worker. SDL defines that caller as its desktop event thread on most
platforms. Cocoa requires the actual process main thread, so the backend checks
that native constraint before SDL initialization and fails startup clearly
when it is violated. The facade still accepts window and input operations from
arbitrary threads. The one runtime:

- starts and stops SDL on its event runner;
- serializes accepted window and input commands;
- pumps and translates SDL events for both subsystems;
- dispatches one native event per turn so semantic work from an earlier input
  event cannot be overtaken by a later window event;
- wakes promptly when another thread queues work; and
- drains work admitted before shutdown closes the queue.

This arrangement prevents separate window and input loops from racing over the
same native queue. Public event callbacks run in serialized manager order on
the event runner, with no manager or platform lock held. Callbacks may safely
inspect state, reset subscriptions, or initiate asynchronous work, but should
remain short so they do not stall desktop event processing.

SDL's quit event is translated into Tyrant close requests. The backend disables
SDL's last-window process policy, so `GuiApplication`, `WindowSession`, and
`ApplicationLifetime` remain responsible for deciding when the process stops.
No nested modal event loop is introduced.

## Effective behavior and capabilities

Window creation is best-effort. Callers provide a `WindowDescriptor`, then
inspect the window's effective `WindowConfiguration` and
`WindowCapabilities`. Mutations are asynchronous and return an applied,
normalized, cancelled, or error result without invalidating an otherwise live
window.

The SDL backend currently applies these notable rules:

- every window requests high-pixel-density support;
- logical content bounds, physical framebuffer size, and scale are observed
  independently and published as one coherent geometry update;
- independently configurable minimize, maximize, and close buttons normalize
  to SDL's decorated-versus-borderless model;
- a `Child` request becomes a semantic dialog when it has a parent, or a
  top-level window otherwise, because SDL has no portable embedded child
  surface;
- a dialog or modal role without a parent normalizes to top-level;
- parent, tool, and dialog relationships are maintained by Tyrant rather than
  SDL native ownership, so closing a parent cannot override `WindowSession`
  scope policy by recursively destroying retained children;
- embedded child windows and independent per-axis scaling are reported
  unsupported;
- positioning and always-on-top are reported unavailable on Wayland, where the
  compositor owns those decisions; and
- modality is implemented without blocking: the Graphics facade uses the
  backend's input-control capability to suppress and later restore affected
  windows.

Close requests remain cancellable. Resize, move, scale, state, focus, input,
configuration, and close events are translated to typed Graphics events.
Unsupported preferences remain visible through capabilities and effective
configuration rather than leaking SDL-specific behavior into callers.

## Build, install, and license

The desktop backend is enabled by default and can be omitted at configure time:

```bash
cmake --preset linux-x64-debug -DTGE_MODULE_SDLDESKTOP=OFF
```

When enabled, configuration fetches the exact SDL 3.4.12 release archive and
verifies its pinned SHA-256 digest. Tyrant builds only SDL's shared runtime;
SDL tests, examples, documentation, and upstream install rules are disabled.
Optional X11 extensions are disabled individually when their development
packages are unavailable, without unnecessarily disabling the base X11
backend.

SDL remains a private implementation dependency, but its shared runtime is a
deployment dependency for both static and shared Tyrant builds. Installation
therefore includes the SDL runtime artifact and its license notice. Installed
static packages also provide the internal imported target needed to resolve
that shared dependency. Runtime search paths are configured so a shared Tyrant
installation can find the colocated SDL library.

The pinned release and its source are available from the
[SDL 3.4.12 release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.12).

## Explicit exclusions

The SDL desktop decision does not authorize use of:

- SDL rendering, drawing primitives, renderer-managed surfaces, textures, GPU
  devices, commands, or presentation policy;
- SDL-created Vulkan surfaces, OpenGL or OpenGL ES contexts, or Metal views;
- SDL audio, camera, haptic, power, sensor, dialog, tray, filesystem, asset,
  timing, job, allocation, or logging services;
- SDL process entrypoint or application-lifecycle ownership;
- a public SDL facade, native-handle accessor, property bag, or service
  locator; or
- direct SDL use outside the private `SDLDesktop` integration island.

The build disables SDL GPU, renderer, Vulkan, OpenGL, OpenGL ES, and Metal
integration to make the rendering boundary concrete. A future renderer-to-
window bridge must use Tyrant window identity behind private contracts.
Rendering continues to own devices, queues, contexts, surfaces, swapchains,
synchronization, resources, pipelines, and commands.

Using SDL for another engine subsystem requires a separate architectural
decision. The fact that window and input events share SDL's native queue does
not transfer ownership of Input's public model to SDL.

## Native-backend canary

Before SDL is authorized for another major subsystem, or before the desktop
contracts are declared stable, Tyrant should implement at least one native
window and input provider. Win32 remains the preferred first canary because its
threading, DPI, parenting, modality, raw input, and tooling behavior will expose
SDL-shaped assumptions quickly.

The native provider must satisfy the same platform-contract, concurrency,
event-ordering, installed-consumer, and Editor smoke behavior as SDL. SDL may
remain a broad portable backend, but neither implementation becomes the public
specification merely by arriving first.
