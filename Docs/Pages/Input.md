# Input

The Input module owns Tyrant's public model for keyboard, text, pointer, touch,
and attached input-device behavior. Graphics owns windows; a desktop backend
may observe both through one operating-system event queue, but that mechanical
constraint does not merge their public responsibilities.

Applications include the complete public surface with:

```cpp
#include <TGE/Input.hpp>
```

## Ownership boundary

Input owns:

- context identity, lifecycle, configuration, and capabilities;
- typed input events and their portable values;
- coherent state snapshots;
- device descriptions and connection changes;
- asynchronous, result-bearing context mutations;
- subscription lifetime and public event ordering; and
- the caller-safe threading contract.

Graphics contains no keyboard, mouse, touch, gamepad, USB, scan-code, or native
input types. Platform adapters translate native events directly into Input's
private SPI. Native handles, SDL event structures, backend device identifiers,
and raw platform scan codes never enter the public API.

## Managers and contexts

`IInputManager` is a dependency-injection-registerable, thread-safe owner and
directory. It can create and destroy contexts, find one by `InputContextId`,
return an owned context snapshot in creation order, enumerate connected
devices, and publish device changes.

An `IInputContext` is one independently controllable route for input. Its
creation descriptor can request:

- an initially enabled or suppressed route;
- pointer capture; and
- relative pointer mode.

The requested descriptor remains available for diagnostics. The effective
configuration reports what the backend actually applied, while
`InputContextCapabilities` reports whether enable control, capture, relative
pointer mode, keyboard, text, pointer, and touch are supported. Asynchronous
mutations return `Applied`, `Normalized`, or `Cancelled`, or an `InputError`
that can be handled without stopping the manager.

Disabling a context suppresses event publication and state updates without
blocking the native loop. Its next snapshot also clears pressed keys, pointer
buttons, and touch contacts. Capture and relative-mode requests return a
normalized or unsupported result when the target or backend cannot honor them.
Temporary target suppression also releases effective capture, relative mode,
and text-input/IME activation while retaining the caller's requests; they are
restored when the target becomes eligible and focused again.

## Window bridge

Input does not depend on Graphics, and Graphics does not depend on Input. The
GUI integration module owns the narrow join:

```text
IWindowInputContextFactory
  + live IWindow
  -> window-routed IInputContext
```

The bridge accepts only a live Tyrant window and an input descriptor. It uses
opaque engine identity internally; it does not expose a native or SDL window
handle. This keeps headless or device-oriented input contexts possible without
making a window part of Input's base contract.

`GuiApplication::UseDefaultDesktopBackend()` registers the bridge with the
window and input managers. When a configured root window starts, the GUI
coordinator creates its routed input context before user hosted services start.
The root `WindowSession` then exposes `InputContext()` alongside `Window()` so
services can subscribe through `GuiApplicationContext`.

## Typed events

Contexts publish separate event families for:

- physical and logical keyboard input;
- committed UTF-8 text;
- pointer movement;
- pointer buttons;
- pointer wheel motion;
- touch contacts; and
- manager-wide device connection, update, and disconnection.

Keyboard and text are intentionally distinct. A keyboard event contains a
portable physical position, logical name, pressed or released action,
modifiers, and repeat state. A text event contains committed text after the
platform's keyboard layout and input method have done their work.

Pointer and touch positions use the target window's logical content coordinate
space rather than framebuffer pixels. Pointer movement includes a logical
delta and whether relative mode is active. Wheel values carry an explicit unit.
Touch events carry a stable context-local contact value, action, logical
position, and pressure.

Every routed event includes its context, device, manager-wide sequence, and
steady-clock timestamp. Sequence and timestamp values increase monotonically
across all events published by one manager. Device-change events participate
in that same order.

Each subscription returns a move-only `InputSubscription`. Destroying or
resetting the token prevents later queued delivery and is idempotent. Reset is
safe from inside a callback; a callback already running on another thread is
allowed to finish.

## State snapshots

`IInputContext::StateSnapshot()` returns
`std::shared_ptr<const InputStateSnapshot>`. Each snapshot is a coherent,
immutable view after a routed event and contains:

- context identity, event sequence, and timestamp;
- effective context configuration;
- pressed physical keys and modifiers;
- logical pointer position and pressed buttons; and
- active touch contacts.

Retaining a snapshot is safe: later events publish a new object rather than
mutating the old one. This supports deterministic frame sampling without
requiring application code to lock the event stream.

When a bound window is suppressed by modality or explicit window policy, the
backend clears pressed keys and buttons, modifiers, and active touches without
changing the caller-controlled context-enabled setting or fabricating public
release events. Restoring the window therefore resumes a still-enabled input
context without retaining stale transient state.

## Devices

`IInputManager::Devices()` returns an owned snapshot in discovery order.
Descriptors identify a semantic kind such as keyboard, pointer, touch, pen, or
gamepad, together with a portable Tyrant identity, display name, optional
USB-style vendor and product identifiers, and whether the device is virtual.
Those identifiers are descriptive data, not handles.

The current public event surface covers keyboard, text, pointer, and touch
activity plus device lifecycle. Future pen, gamepad control, or general HID
payloads belong in Input as typed contracts; they do not belong in Graphics or
as pass-through SDL events.

## Threading and ordering

Callers may use managers and contexts from arbitrary threads. Public mutations
are asynchronous because a concrete adapter may require one platform event
thread. The facade serializes accepted platform work and publishes callbacks
in manager-wide order without holding manager or platform locks.

With the default desktop backend, window and input adapters share one
externally driven `DesktopEventRuntime`. `GuiApplication::Run()` drives that
runtime on its calling thread, and queued operations wake the event pump.
Callbacks may inspect snapshots, reset subscriptions, and initiate asynchronous
operations safely. They should not block because they execute in serialized
event order.

## Lifetime

The manager owns live contexts. A context moves through `Open`, `Destroying`,
and `Destroyed`; destroying it is asynchronous and idempotent errors are
reported explicitly. Manager shutdown closes all remaining contexts before
releasing platform input state.

A `WindowSession` may own both a routed input context and its window. Scope
cleanup detaches and destroys input routing before destroying the native
window. For a window-owned session, final window closure ends the service scope;
inherited and externally owned scopes keep their existing lifetime policy.
Root-window closure may additionally request application shutdown, but that is
a `GuiApplication` policy rather than an Input responsibility.
