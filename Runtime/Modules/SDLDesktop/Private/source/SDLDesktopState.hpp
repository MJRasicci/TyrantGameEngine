#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "Internal/Desktop/IDesktopEventPump.hpp"
#include "Internal/Graphics/IWindowPlatform.hpp"
#include "Internal/Input/IInputPlatform.hpp"

namespace TGE::Internal
{
    /**
     * @brief SDL-private state shared by the pump and both semantic adapters.
     *
     * SDL event ownership lives here so neither Graphics nor Input can poll
     * the process-wide SDL queue independently.
     */
    class SDLDesktopState final
    {
    public:
        SDLDesktopState() = default;
        ~SDLDesktopState();

        SDLDesktopState(const SDLDesktopState&) = delete;
        SDLDesktopState& operator=(const SDLDesktopState&) = delete;

        [[nodiscard]] DesktopEventResult Start();
        void Wake() noexcept;
        void PumpEvents(std::chrono::milliseconds maxWait) noexcept;
        void Stop() noexcept;

        void SetWindowEventSink(
            IWindowPlatformEventSink* sink) noexcept;
        void ShutdownWindows() noexcept;
        [[nodiscard]] WindowPlatformCreateResult CreateWindow(
            WindowId id,
            const WindowDescriptor& descriptor);
        [[nodiscard]] WindowOperationResult DestroyWindow(WindowId id);
        [[nodiscard]] WindowPlatformMutationResult SetTitle(
            WindowId id,
            std::string title);
        [[nodiscard]] WindowPlatformMutationResult SetLogicalBounds(
            WindowId id,
            LogicalBounds bounds);
        [[nodiscard]] WindowPlatformMutationResult SetState(
            WindowId id,
            WindowState state);
        [[nodiscard]] WindowPlatformMutationResult SetVisible(
            WindowId id,
            bool visible);
        [[nodiscard]] WindowPlatformMutationResult RequestFocus(WindowId id);
        [[nodiscard]] WindowPlatformMutationResult SetInputEnabled(
            WindowId id,
            bool enabled);
        [[nodiscard]] WindowOperationResult RequestClose(WindowId id);

        void SetInputEventSink(
            IInputPlatformEventSink* sink) noexcept;
        [[nodiscard]] std::vector<InputDeviceDescriptor>
            ConnectedDevices() const;
        [[nodiscard]] InputPlatformCreateResult CreateInputContext(
            InputContextId id,
            const InputContextDescriptor& descriptor,
            InputPlatformTarget target);
        [[nodiscard]] InputOperationResult DestroyInputContext(
            InputContextId id);
        [[nodiscard]] InputPlatformMutationResult SetContextEnabled(
            InputContextId id,
            bool enabled);
        [[nodiscard]] InputPlatformMutationResult SetContextCapture(
            InputContextId id,
            bool captured);
        [[nodiscard]] InputPlatformMutationResult SetRelativePointerMode(
            InputContextId id,
            bool enabled);
        void ShutdownInput() noexcept;

    private:
        struct WindowRecord
        {
            WindowId id;
            SDL_Window* window { nullptr };
            WindowDescriptor requested;
            WindowConfiguration configuration;
            bool pointerModesSuspended { false };
        };

        struct InputContextRecord
        {
            InputContextId id;
            InputPlatformTarget target;
            InputContextConfiguration configuration;
            InputContextCapabilities capabilities;
            bool requestedCapture { false };
            bool requestedRelativePointerMode { false };
        };

        enum class DeviceDomain : std::uint64_t
        {
            Keyboard = 1,
            Pointer = 2,
            Touch = 3,
            Gamepad = 4,
            Pen = 5
        };

        void DispatchEvent(const SDL_Event& event) noexcept;
        void DispatchWindowEvent(const SDL_WindowEvent& event) noexcept;
        void DispatchKeyboardEvent(const SDL_KeyboardEvent& event) noexcept;
        void DispatchTextEvent(const SDL_TextInputEvent& event) noexcept;
        void DispatchPointerMovedEvent(
            const SDL_MouseMotionEvent& event) noexcept;
        void DispatchPointerButtonEvent(
            const SDL_MouseButtonEvent& event) noexcept;
        void DispatchPointerWheelEvent(
            const SDL_MouseWheelEvent& event) noexcept;
        void DispatchTouchEvent(
            const SDL_TouchFingerEvent& event) noexcept;
        void DispatchDeviceEvent(const SDL_Event& event) noexcept;
        void DispatchQuitRequest() noexcept;
        [[nodiscard]] bool DestroyNativeWindow(
            WindowId id,
            WindowCloseReason reason,
            bool notify) noexcept;
        void PrepareWindowRemoval(WindowId id) noexcept;
        void DetachChildren(WindowId parent) noexcept;
        void InvalidateRemovedInputTarget(WindowId window) noexcept;
        [[nodiscard]] std::vector<WindowId>
            WindowsChildFirst() const;

        [[nodiscard]] WindowRecord* FindWindow(WindowId id) noexcept;
        [[nodiscard]] const WindowRecord* FindWindow(
            WindowId id) const noexcept;
        [[nodiscard]] WindowRecord* FindWindow(
            SDL_WindowID id) noexcept;
        [[nodiscard]] const WindowRecord* FindWindow(
            SDL_WindowID id) const noexcept;

        [[nodiscard]] WindowConfiguration QueryConfiguration(
            const WindowRecord& record) const;
        [[nodiscard]] WindowCapabilities QueryCapabilities(
            const WindowRecord& record) const noexcept;
        [[nodiscard]] WindowPlatformMutationResult MutationResult(
            WindowRecord& record,
            WindowOperationStatus status =
                WindowOperationStatus::Applied);
        void PublishConfiguration(WindowRecord& record) noexcept;

        [[nodiscard]] std::vector<InputContextRecord>
            InputTargetsForWindow(SDL_WindowID window) const;
        void RefreshTargetPointerModes(
            WindowId window,
            bool inputInvalidated) noexcept;
        [[nodiscard]] SDL_Window* ResolveTarget(
            InputPlatformTarget target) const noexcept;
        void RefreshTextInput(InputPlatformTarget target) noexcept;

        [[nodiscard]] static InputDeviceId MakeDeviceId(
            DeviceDomain domain,
            std::uint64_t nativeId) noexcept;
        [[nodiscard]] InputDeviceDescriptor DescribeKeyboard(
            SDL_KeyboardID id) const;
        [[nodiscard]] InputDeviceDescriptor DescribePointer(
            SDL_MouseID id) const;
        [[nodiscard]] InputDeviceDescriptor DescribeTouch(
            SDL_TouchID id) const;
        [[nodiscard]] InputDeviceDescriptor DescribeGamepad(
            SDL_JoystickID id) const;
        void RefreshConnectedDevices();
        void AddOrUpdateDevice(
            InputDeviceChangeKind change,
            InputDeviceDescriptor device) noexcept;
        void RemoveDevice(InputDeviceId id) noexcept;

        [[nodiscard]] static InputModifiers TranslateModifiers(
            SDL_Keymod modifiers) noexcept;
        [[nodiscard]] static PhysicalKeyCode TranslatePhysicalKey(
            SDL_Scancode scancode) noexcept;
        [[nodiscard]] static PointerButton TranslatePointerButton(
            std::uint8_t button) noexcept;
        [[nodiscard]] static TouchAction TranslateTouchAction(
            SDL_EventType event) noexcept;

        [[nodiscard]] static WindowError WindowFailure(
            std::string operation);
        [[nodiscard]] static WindowError MissingWindow(WindowId id);
        [[nodiscard]] static InputError InputFailure(
            std::string operation);
        [[nodiscard]] static InputError MissingContext(InputContextId id);

        mutable std::mutex mutex;
        mutable std::recursive_mutex windowSinkMutex;
        mutable std::recursive_mutex inputSinkMutex;
        IWindowPlatformEventSink* windowSink { nullptr };
        IInputPlatformEventSink* inputSink { nullptr };
        std::unordered_map<WindowId, WindowRecord> windows;
        std::unordered_map<SDL_WindowID, WindowId> windowIds;
        std::unordered_map<InputContextId, InputContextRecord> inputContexts;
        std::unordered_map<InputDeviceId, InputDeviceDescriptor> devices;
        std::vector<InputDeviceId> deviceOrder;
        std::atomic<Uint32> wakeEventType { 0 };
        std::atomic_bool started { false };
    };

    [[nodiscard]] std::unique_ptr<IDesktopEventPump>
        CreateSDLDesktopEventPump(
            std::shared_ptr<SDLDesktopState> state);
    [[nodiscard]] std::unique_ptr<IWindowPlatform>
        CreateSDLWindowPlatform(
            std::shared_ptr<SDLDesktopState> state);
    [[nodiscard]] std::unique_ptr<IInputPlatform>
        CreateSDLInputPlatform(
            std::shared_ptr<SDLDesktopState> state);
}
