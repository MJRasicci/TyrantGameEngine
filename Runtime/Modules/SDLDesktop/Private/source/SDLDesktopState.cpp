#include "SDLDesktopState.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

#if defined(__APPLE__)
#include <pthread.h>
#endif

namespace TGE::Internal
{
    namespace
    {
        constexpr SDL_InitFlags DesktopSubsystems =
            SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;

        int ToSDLCoordinate(float value) noexcept
        {
            const auto rounded = std::llround(value);
            return static_cast<int>(std::clamp(
                rounded,
                static_cast<long long>(
                    std::numeric_limits<int>::min()),
                static_cast<long long>(
                    std::numeric_limits<int>::max())));
        }

        int ToSDLSize(float value) noexcept
        {
            return std::max(1, ToSDLCoordinate(value));
        }

        float WindowToLogicalScale(SDL_Window* window) noexcept
        {
            const auto pixelDensity =
                SDL_GetWindowPixelDensity(window);
            const auto displayScale =
                SDL_GetWindowDisplayScale(window);
            if (pixelDensity > 0.0F && displayScale > 0.0F)
            {
                return pixelDensity / displayScale;
            }
            return 1.0F;
        }

        float LogicalToWindowScale(SDL_Window* window) noexcept
        {
            const auto scale = WindowToLogicalScale(window);
            return scale > 0.0F ? 1.0F / scale : 1.0F;
        }

        int ToSDLWindowCoordinate(
            SDL_Window* window,
            float logicalValue) noexcept
        {
            return ToSDLCoordinate(
                logicalValue * LogicalToWindowScale(window));
        }

        int ToSDLWindowSize(
            SDL_Window* window,
            float logicalValue) noexcept
        {
            return std::max(
                1,
                ToSDLWindowCoordinate(window, logicalValue));
        }

        std::uint32_t ToFramebufferDimension(int value) noexcept
        {
            return value <= 0
                ? 0U
                : static_cast<std::uint32_t>(value);
        }

        bool NearlyEqual(float left, float right) noexcept
        {
            return std::abs(left - right) <= 0.5F;
        }

        bool SameBounds(
            const LogicalBounds& left,
            const LogicalBounds& right) noexcept
        {
            return NearlyEqual(left.position.x, right.position.x) &&
                NearlyEqual(left.position.y, right.position.y) &&
                NearlyEqual(left.size.width, right.size.width) &&
                NearlyEqual(left.size.height, right.size.height);
        }

        std::string CopyString(const char* value)
        {
            return value == nullptr
                ? std::string {}
                : std::string(value);
        }

        class SDLProperties final
        {
        public:
            SDLProperties()
                : id(SDL_CreateProperties())
            {
            }

            ~SDLProperties()
            {
                if (id != 0)
                {
                    SDL_DestroyProperties(id);
                }
            }

            SDLProperties(const SDLProperties&) = delete;
            SDLProperties& operator=(const SDLProperties&) = delete;

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return id != 0;
            }

            [[nodiscard]] SDL_PropertiesID Id() const noexcept
            {
                return id;
            }

        private:
            SDL_PropertiesID id { 0 };
        };

        bool SetBoolean(
            SDL_PropertiesID properties,
            const char* name,
            bool value) noexcept
        {
            return SDL_SetBooleanProperty(properties, name, value);
        }

        bool SetNumber(
            SDL_PropertiesID properties,
            const char* name,
            std::int64_t value) noexcept
        {
            return SDL_SetNumberProperty(
                properties,
                name,
                static_cast<Sint64>(value));
        }

        bool SetString(
            SDL_PropertiesID properties,
            const char* name,
            const std::string& value) noexcept
        {
            return SDL_SetStringProperty(
                properties,
                name,
                value.c_str());
        }

        WindowRole NormalizeRole(
            WindowRole requested,
            bool hasParent) noexcept
        {
            switch (requested)
            {
                case WindowRole::Child:
                    // SDL can express native ownership, but not a portable
                    // embedded child surface.
                    return hasParent
                        ? WindowRole::Dialog
                        : WindowRole::TopLevel;
                case WindowRole::Dialog:
                case WindowRole::Modal:
                    return hasParent
                        ? requested
                        : WindowRole::TopLevel;
                default:
                    return requested;
            }
        }

        std::optional<std::uint16_t> OptionalIdentifier(
            std::uint16_t value) noexcept
        {
            if (value == 0)
            {
                return std::nullopt;
            }
            return value;
        }
    }

    SDLDesktopState::~SDLDesktopState()
    {
        Stop();
    }

    DesktopEventResult SDLDesktopState::Start()
    {
        if (started.load())
        {
            return std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::InvalidState,
                .message = "The SDL desktop event pump is already running."
            });
        }

#if defined(__APPLE__)
        // SDL cannot identify the process entry thread before its first
        // initialization call. Cocoa can, so reject before SDL records an
        // invalid caller as its event thread and poisons a later retry.
        if (pthread_main_np() == 0)
        {
            return std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::PumpStartFailed,
                .message =
                    "SDL desktop initialization must run on the process "
                    "main thread."
            });
        }
#endif

        // SDL's synthetic application quit event must not own Tyrant's
        // process lifetime. GuiApplication applies root-window policy above
        // the Graphics and Input subsystems.
        (void)SDL_SetHint(
            SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE,
            "0");

        if (!SDL_Init(DesktopSubsystems))
        {
            return std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::PumpStartFailed,
                .message = std::format(
                    "SDL desktop initialization failed: {}",
                    SDL_GetError())
            });
        }

        const auto registered = SDL_RegisterEvents(1);
        if (registered == 0)
        {
            SDL_QuitSubSystem(DesktopSubsystems);
            return std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::PumpStartFailed,
                .message = std::format(
                    "SDL could not reserve Tyrant's wake event: {}",
                    SDL_GetError())
            });
        }

        wakeEventType.store(registered);
        started.store(true);
        try
        {
            RefreshConnectedDevices();
        }
        catch (const std::exception& exception)
        {
            const auto message = std::format(
                "SDL input-device discovery failed: {}",
                exception.what());
            Stop();
            return std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::PumpStartFailed,
                .message = std::move(message)
            });
        }
        catch (...)
        {
            Stop();
            return std::unexpected(DesktopEventError {
                .code = DesktopEventErrorCode::PumpStartFailed,
                .message =
                    "SDL input-device discovery failed with an unknown error."
            });
        }
        return {};
    }

    void SDLDesktopState::Wake() noexcept
    {
        const auto eventType = wakeEventType.load();
        if (!started.load() || eventType == 0)
        {
            return;
        }

        SDL_Event event {};
        event.type = eventType;
        (void)SDL_PushEvent(&event);
    }

    void SDLDesktopState::PumpEvents(
        std::chrono::milliseconds maxWait) noexcept
    {
        if (!started.load())
        {
            return;
        }

        const auto boundedWait = std::clamp<std::int64_t>(
            maxWait.count(),
            0,
            std::numeric_limits<Sint32>::max());
        SDL_Event event {};
        if (SDL_WaitEventTimeout(
                &event,
                static_cast<Sint32>(boundedWait)))
        {
            DispatchEvent(event);
        }
    }

    void SDLDesktopState::Stop() noexcept
    {
        if (!started.exchange(false))
        {
            return;
        }

        ShutdownInput();
        ShutdownWindows();

        {
            std::scoped_lock lock(mutex);
            devices.clear();
            deviceOrder.clear();
        }

        wakeEventType.store(0);
        SDL_QuitSubSystem(DesktopSubsystems);
    }

    void SDLDesktopState::SetWindowEventSink(
        IWindowPlatformEventSink* sink) noexcept
    {
        std::scoped_lock lock(windowSinkMutex);
        windowSink = sink;
    }

    void SDLDesktopState::ShutdownWindows() noexcept
    {
        const auto ordered = WindowsChildFirst();
        std::vector<SDL_Window*> remaining;
        remaining.reserve(ordered.size());
        for (const auto id : ordered)
        {
            auto found = windows.find(id);
            if (found == windows.end())
            {
                continue;
            }
            PrepareWindowRemoval(id);
            auto* window = found->second.window;
            if (window != nullptr)
            {
                (void)SDL_StopTextInput(window);
                windowIds.erase(SDL_GetWindowID(window));
                remaining.emplace_back(window);
            }
            windows.erase(found);
        }
        windowIds.clear();

        for (auto* window : remaining)
        {
            (void)SDL_StopTextInput(window);
            SDL_DestroyWindow(window);
        }
    }

    void SDLDesktopState::SetInputEventSink(
        IInputPlatformEventSink* sink) noexcept
    {
        std::scoped_lock lock(inputSinkMutex);
        inputSink = sink;
    }

    WindowPlatformCreateResult SDLDesktopState::CreateWindow(
        WindowId id,
        const WindowDescriptor& descriptor)
    {
        if (!started.load())
        {
            return std::unexpected(WindowError {
                .code = WindowErrorCode::InvalidState,
                .message =
                    "SDL desktop services have not been started."
            });
        }
        if (!id || windows.contains(id))
        {
            return std::unexpected(WindowError {
                .code = WindowErrorCode::InvalidDescriptor,
                .message = "The window identifier is invalid or in use."
            });
        }

        if (descriptor.parent)
        {
            const auto* parent = FindWindow(*descriptor.parent);
            if (parent == nullptr)
            {
                return std::unexpected(WindowError {
                    .code = WindowErrorCode::ParentNotFound,
                    .message = std::format(
                        "SDL cannot find parent window {}.",
                        descriptor.parent->Value())
                });
            }
        }

        SDLProperties properties;
        if (!properties)
        {
            return std::unexpected(WindowFailure(
                "create window properties"));
        }

        const auto propertyId = properties.Id();
        bool propertiesSet = true;
        propertiesSet = SetString(
            propertyId,
            SDL_PROP_WINDOW_CREATE_TITLE_STRING,
            descriptor.title) && propertiesSet;
        propertiesSet = SetNumber(
            propertyId,
            SDL_PROP_WINDOW_CREATE_X_NUMBER,
            ToSDLCoordinate(descriptor.bounds.position.x)) &&
            propertiesSet;
        propertiesSet = SetNumber(
            propertyId,
            SDL_PROP_WINDOW_CREATE_Y_NUMBER,
            ToSDLCoordinate(descriptor.bounds.position.y)) &&
            propertiesSet;
        propertiesSet = SetNumber(
            propertyId,
            SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
            ToSDLSize(descriptor.bounds.size.width)) &&
            propertiesSet;
        propertiesSet = SetNumber(
            propertyId,
            SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
            ToSDLSize(descriptor.bounds.size.height)) &&
            propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN,
            true) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN,
            !descriptor.chrome.decorations) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,
            descriptor.chrome.resizable) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN,
            descriptor.alwaysOnTop) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN,
            descriptor.acceptsInput) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,
            true) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN,
            descriptor.role == WindowRole::Tool) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_MODAL_BOOLEAN,
            false) && propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
            descriptor.state == WindowState::Fullscreen) &&
            propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_MINIMIZED_BOOLEAN,
            descriptor.state == WindowState::Minimized) &&
            propertiesSet;
        propertiesSet = SetBoolean(
            propertyId,
            SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN,
            descriptor.state == WindowState::Maximized) &&
            propertiesSet;
        if (!propertiesSet)
        {
            return std::unexpected(WindowFailure(
                "configure window creation"));
        }

        auto* window = SDL_CreateWindowWithProperties(propertyId);
        if (window == nullptr)
        {
            return std::unexpected(WindowFailure("create a window"));
        }

        const auto sdlId = SDL_GetWindowID(window);
        if (sdlId == 0)
        {
            auto failure = WindowFailure("query the new window identifier");
            SDL_DestroyWindow(window);
            return std::unexpected(std::move(failure));
        }

        // SDL window coordinates and Tyrant logical coordinates differ on
        // platforms whose pixel density and content display scale are not the
        // same (notably per-monitor-DPI Windows). Configure the final bounds
        // only after SDL can report both values for the new window.
        (void)SDL_SetWindowPosition(
            window,
            ToSDLWindowCoordinate(window, descriptor.bounds.position.x),
            ToSDLWindowCoordinate(window, descriptor.bounds.position.y));
        (void)SDL_SetWindowSize(
            window,
            ToSDLWindowSize(window, descriptor.bounds.size.width),
            ToSDLWindowSize(window, descriptor.bounds.size.height));
        if (descriptor.initiallyVisible)
        {
            (void)SDL_ShowWindow(window);
        }

        WindowConfiguration configuration {
            .title = descriptor.title,
            .role = NormalizeRole(
                descriptor.role,
                descriptor.parent.has_value()),
            .parent = descriptor.parent,
            .geometry = WindowGeometry {
                .logicalBounds = descriptor.bounds,
                .framebufferSize = FramebufferSize {
                    .width = static_cast<std::uint32_t>(
                        ToSDLSize(descriptor.bounds.size.width)),
                    .height = static_cast<std::uint32_t>(
                        ToSDLSize(descriptor.bounds.size.height))
                },
                .scale = WindowScale {}
            },
            .state = descriptor.state,
            .chrome = WindowChrome {
                .decorations = descriptor.chrome.decorations,
                .resizable = descriptor.chrome.resizable,
                .minimizable = descriptor.chrome.decorations,
                .maximizable = descriptor.chrome.decorations,
                .closable = descriptor.chrome.decorations
            },
            .modality = descriptor.modality,
            .visible = descriptor.initiallyVisible,
            .alwaysOnTop = descriptor.alwaysOnTop,
            .inputEnabled = descriptor.acceptsInput,
            .focused = false
        };

        auto [inserted, didInsert] = windows.emplace(
            id,
            WindowRecord {
                .id = id,
                .window = window,
                .requested = descriptor,
                .configuration = std::move(configuration)
            });
        if (!didInsert)
        {
            SDL_DestroyWindow(window);
            return std::unexpected(WindowError {
                .code = WindowErrorCode::InvalidState,
                .message = "The SDL window identity map rejected a window."
            });
        }
        windowIds.emplace(sdlId, id);

        auto& record = inserted->second;
        record.configuration = QueryConfiguration(record);
        record.pointerModesSuspended =
            !record.configuration.inputEnabled ||
            !record.configuration.focused;
        return WindowPlatformState {
            .configuration = record.configuration,
            .capabilities = QueryCapabilities(record)
        };
    }

    WindowOperationResult SDLDesktopState::DestroyWindow(WindowId id)
    {
        if (!windows.contains(id))
        {
            return std::unexpected(MissingWindow(id));
        }

        (void)DestroyNativeWindow(
            id,
            WindowCloseReason::ApplicationRequest,
            false);
        return WindowOperationStatus::Applied;
    }

    WindowPlatformMutationResult SDLDesktopState::SetTitle(
        WindowId id,
        std::string title)
    {
        auto* record = FindWindow(id);
        if (record == nullptr)
        {
            return std::unexpected(MissingWindow(id));
        }
        if (!SDL_SetWindowTitle(record->window, title.c_str()))
        {
            return std::unexpected(WindowFailure("set the window title"));
        }

        auto result = MutationResult(*record);
        if (result && result->configuration.title != title)
        {
            result->status = WindowOperationStatus::Normalized;
        }
        return result;
    }

    WindowPlatformMutationResult SDLDesktopState::SetLogicalBounds(
        WindowId id,
        LogicalBounds bounds)
    {
        auto* record = FindWindow(id);
        if (record == nullptr)
        {
            return std::unexpected(MissingWindow(id));
        }

        const bool positioned = SDL_SetWindowPosition(
            record->window,
            ToSDLWindowCoordinate(
                record->window,
                bounds.position.x),
            ToSDLWindowCoordinate(
                record->window,
                bounds.position.y));
        const bool sized = SDL_SetWindowSize(
            record->window,
            ToSDLWindowSize(
                record->window,
                bounds.size.width),
            ToSDLWindowSize(
                record->window,
                bounds.size.height));
        if (!positioned && !sized)
        {
            return std::unexpected(WindowFailure(
                "set the window bounds"));
        }

        auto result = MutationResult(
            *record,
            positioned && sized
                ? WindowOperationStatus::Applied
                : WindowOperationStatus::Normalized);
        if (result &&
            !SameBounds(
                result->configuration.geometry.logicalBounds,
                bounds))
        {
            result->status = WindowOperationStatus::Normalized;
        }
        return result;
    }

    WindowPlatformMutationResult SDLDesktopState::SetState(
        WindowId id,
        WindowState state)
    {
        auto* record = FindWindow(id);
        if (record == nullptr)
        {
            return std::unexpected(MissingWindow(id));
        }

        const auto flags = SDL_GetWindowFlags(record->window);
        bool anyApplied = false;
        bool allApplied = true;
        auto apply = [&anyApplied, &allApplied](bool applied)
        {
            anyApplied = anyApplied || applied;
            allApplied = allApplied && applied;
        };

        if (state != WindowState::Fullscreen &&
            (flags & SDL_WINDOW_FULLSCREEN) != 0)
        {
            apply(SDL_SetWindowFullscreen(record->window, false));
        }

        switch (state)
        {
            case WindowState::Normal:
                apply(SDL_RestoreWindow(record->window));
                break;
            case WindowState::Minimized:
                apply(SDL_MinimizeWindow(record->window));
                break;
            case WindowState::Maximized:
                apply(SDL_MaximizeWindow(record->window));
                break;
            case WindowState::Fullscreen:
                apply(SDL_SetWindowFullscreen(record->window, true));
                break;
        }

        if (!anyApplied)
        {
            return std::unexpected(WindowFailure(
                "change the window state"));
        }

        auto result = MutationResult(
            *record,
            allApplied
                ? WindowOperationStatus::Applied
                : WindowOperationStatus::Normalized);
        if (result && result->configuration.state != state)
        {
            // Window-manager transitions are often asynchronous. The later
            // SDL window event supplies the authoritative state.
            result->status = WindowOperationStatus::Normalized;
        }
        return result;
    }

    WindowPlatformMutationResult SDLDesktopState::SetVisible(
        WindowId id,
        bool visible)
    {
        auto* record = FindWindow(id);
        if (record == nullptr)
        {
            return std::unexpected(MissingWindow(id));
        }

        const bool applied = visible
            ? SDL_ShowWindow(record->window)
            : SDL_HideWindow(record->window);
        if (!applied)
        {
            return std::unexpected(WindowFailure(
                visible ? "show the window" : "hide the window"));
        }

        auto result = MutationResult(*record);
        if (result && result->configuration.visible != visible)
        {
            result->status = WindowOperationStatus::Normalized;
        }
        return result;
    }

    WindowPlatformMutationResult SDLDesktopState::RequestFocus(WindowId id)
    {
        auto* record = FindWindow(id);
        if (record == nullptr)
        {
            return std::unexpected(MissingWindow(id));
        }
        if (!SDL_RaiseWindow(record->window))
        {
            return std::unexpected(WindowFailure("raise the window"));
        }

        auto result = MutationResult(*record);
        if (result && !result->configuration.focused)
        {
            result->status = WindowOperationStatus::Normalized;
        }
        return result;
    }

    WindowPlatformMutationResult SDLDesktopState::SetInputEnabled(
        WindowId id,
        bool enabled)
    {
        auto* record = FindWindow(id);
        if (record == nullptr)
        {
            return std::unexpected(MissingWindow(id));
        }

        const auto wasEnabled =
            record->configuration.inputEnabled;
        const auto applied =
            SDL_SetWindowFocusable(record->window, enabled);

        // Tyrant's routing policy remains enforceable even when a compositor
        // cannot apply the corresponding native focusability preference.
        record->configuration.inputEnabled = enabled;
        record->pointerModesSuspended =
            !enabled || !record->configuration.focused;
        if (wasEnabled != enabled)
        {
            RefreshTargetPointerModes(
                id,
                wasEnabled && !enabled);
            RefreshTextInput(
                InputPlatformTarget::FromValue(id.Value()));
        }

        auto result = MutationResult(
            *record,
            applied
                ? WindowOperationStatus::Applied
                : WindowOperationStatus::Normalized);
        if (result)
        {
            // SDL reports focusability through a flag but some compositors
            // apply it asynchronously. Successful explicit control is the
            // effective policy used by the Input adapter immediately.
            result->configuration.inputEnabled = enabled;
            record->configuration.inputEnabled = enabled;
        }
        return result;
    }

    WindowOperationResult SDLDesktopState::RequestClose(WindowId id)
    {
        auto found = windows.find(id);
        if (found == windows.end())
        {
            return std::unexpected(MissingWindow(id));
        }

        std::scoped_lock sinkLock(windowSinkMutex);
        auto* sink = windowSink;
        if (sink != nullptr &&
            !sink->OnPlatformCloseRequested(
                id,
                WindowCloseReason::ApplicationRequest))
        {
            return WindowOperationStatus::Cancelled;
        }

        // A close callback may synchronously cause a higher-level destroy.
        found = windows.find(id);
        if (found == windows.end())
        {
            return WindowOperationStatus::Applied;
        }

        (void)sink;
        (void)DestroyNativeWindow(
            id,
            WindowCloseReason::ApplicationRequest,
            true);
        return WindowOperationStatus::Applied;
    }

    bool SDLDesktopState::DestroyNativeWindow(
        WindowId id,
        WindowCloseReason reason,
        bool notify) noexcept
    {
        try
        {
            auto found = windows.find(id);
            if (found == windows.end())
            {
                return false;
            }

            PrepareWindowRemoval(id);
            DetachChildren(id);
            found = windows.find(id);
            if (found == windows.end())
            {
                return false;
            }

            auto* window = found->second.window;
            const auto sdlId = SDL_GetWindowID(window);
            (void)SDL_StopTextInput(window);
            windowIds.erase(sdlId);
            windows.erase(found);
            SDL_DestroyWindow(window);

            if (notify)
            {
                std::scoped_lock sinkLock(windowSinkMutex);
                if (auto* sink = windowSink)
                {
                    sink->OnPlatformClosed(id, reason);
                }
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void SDLDesktopState::PrepareWindowRemoval(
        WindowId id) noexcept
    {
        auto* record = FindWindow(id);
        if (record == nullptr)
        {
            return;
        }

        record->configuration.inputEnabled = false;
        record->pointerModesSuspended = true;
        RefreshTargetPointerModes(id, true);
        RefreshTextInput(
            InputPlatformTarget::FromValue(id.Value()));
    }

    void SDLDesktopState::DetachChildren(
        WindowId parent) noexcept
    {
        std::vector<WindowId> children;
        for (const auto& [id, record] : windows)
        {
            if (record.configuration.parent == parent)
            {
                children.emplace_back(id);
            }
        }

        for (const auto childId : children)
        {
            auto* child = FindWindow(childId);
            if (child == nullptr)
            {
                continue;
            }

            // Parentage is a Tyrant semantic relationship, not an SDL native
            // ownership link. SDL recursively destroys native children, which
            // would let a platform detail override WindowSession scope policy.
            child->configuration.parent.reset();
            child->configuration.role =
                NormalizeRole(child->requested.role, false);
            if (child->configuration.modality ==
                    WindowModality::DisableParent ||
                child->configuration.modality ==
                    WindowModality::DisableParentTree)
            {
                child->configuration.modality =
                    WindowModality::Modeless;
            }
            PublishConfiguration(*child);
        }
    }

    void SDLDesktopState::InvalidateRemovedInputTarget(
        WindowId window) noexcept
    {
        bool wantsCapture = false;
        for (const auto& [id, context] : inputContexts)
        {
            (void)id;
            if (!context.configuration.enabled ||
                !context.requestedCapture ||
                context.target.Value() == window.Value())
            {
                continue;
            }

            const auto* contextWindow = FindWindow(
                WindowId::FromValue(context.target.Value()));
            if (contextWindow != nullptr &&
                contextWindow->configuration.inputEnabled &&
                contextWindow->configuration.focused &&
                !contextWindow->pointerModesSuspended)
            {
                wantsCapture = true;
                break;
            }
        }
        (void)SDL_CaptureMouse(wantsCapture);

        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        for (auto& [id, context] : inputContexts)
        {
            if (context.target.Value() != window.Value())
            {
                continue;
            }
            context.configuration.captured = false;
            context.configuration.relativePointerMode = false;
            if (sink != nullptr)
            {
                sink->OnTargetInputChanged(
                    id,
                    context.configuration,
                    true);
            }
        }
    }

    std::vector<WindowId>
        SDLDesktopState::WindowsChildFirst() const
    {
        std::vector<std::pair<std::size_t, WindowId>> ranked;
        ranked.reserve(windows.size());
        for (const auto& [id, record] : windows)
        {
            std::size_t depth = 0;
            auto parent = record.configuration.parent;
            while (parent && depth < windows.size())
            {
                const auto found = windows.find(*parent);
                if (found == windows.end())
                {
                    break;
                }
                ++depth;
                parent = found->second.configuration.parent;
            }
            ranked.emplace_back(depth, id);
        }

        std::ranges::sort(
            ranked,
            [](const auto& left, const auto& right)
            {
                if (left.first != right.first)
                {
                    return left.first > right.first;
                }
                return left.second.Value() > right.second.Value();
            });

        std::vector<WindowId> result;
        result.reserve(ranked.size());
        for (const auto& [depth, id] : ranked)
        {
            (void)depth;
            result.emplace_back(id);
        }
        return result;
    }

    SDLDesktopState::WindowRecord* SDLDesktopState::FindWindow(
        WindowId id) noexcept
    {
        const auto found = windows.find(id);
        return found == windows.end()
            ? nullptr
            : &found->second;
    }

    const SDLDesktopState::WindowRecord* SDLDesktopState::FindWindow(
        WindowId id) const noexcept
    {
        const auto found = windows.find(id);
        return found == windows.end()
            ? nullptr
            : &found->second;
    }

    SDLDesktopState::WindowRecord* SDLDesktopState::FindWindow(
        SDL_WindowID id) noexcept
    {
        const auto found = windowIds.find(id);
        return found == windowIds.end()
            ? nullptr
            : FindWindow(found->second);
    }

    const SDLDesktopState::WindowRecord* SDLDesktopState::FindWindow(
        SDL_WindowID id) const noexcept
    {
        const auto found = windowIds.find(id);
        return found == windowIds.end()
            ? nullptr
            : FindWindow(found->second);
    }

    WindowConfiguration SDLDesktopState::QueryConfiguration(
        const WindowRecord& record) const
    {
        auto configuration = record.configuration;
        const auto flags = SDL_GetWindowFlags(record.window);

        configuration.title =
            CopyString(SDL_GetWindowTitle(record.window));
        configuration.visible =
            (flags & SDL_WINDOW_HIDDEN) == 0;
        configuration.alwaysOnTop =
            (flags & SDL_WINDOW_ALWAYS_ON_TOP) != 0;
        configuration.focused =
            (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
        configuration.chrome.decorations =
            (flags & SDL_WINDOW_BORDERLESS) == 0;
        configuration.chrome.resizable =
            (flags & SDL_WINDOW_RESIZABLE) != 0;

        // SDL supports border and resizable toggles, but not independent
        // minimize, maximize, and close controls on a decorated window.
        configuration.chrome.minimizable =
            configuration.chrome.decorations;
        configuration.chrome.maximizable =
            configuration.chrome.decorations;
        configuration.chrome.closable =
            configuration.chrome.decorations;

        if ((flags & SDL_WINDOW_FULLSCREEN) != 0)
        {
            configuration.state = WindowState::Fullscreen;
        }
        else if ((flags & SDL_WINDOW_MINIMIZED) != 0)
        {
            configuration.state = WindowState::Minimized;
        }
        else if ((flags & SDL_WINDOW_MAXIMIZED) != 0)
        {
            configuration.state = WindowState::Maximized;
        }
        else
        {
            configuration.state = WindowState::Normal;
        }

        int x = 0;
        int y = 0;
        const auto windowToLogical =
            WindowToLogicalScale(record.window);
        if (SDL_GetWindowPosition(record.window, &x, &y))
        {
            configuration.geometry.logicalBounds.position = {
                .x = static_cast<float>(x) * windowToLogical,
                .y = static_cast<float>(y) * windowToLogical
            };
        }

        int width = 0;
        int height = 0;
        if (SDL_GetWindowSize(record.window, &width, &height))
        {
            configuration.geometry.logicalBounds.size = {
                .width =
                    static_cast<float>(width) * windowToLogical,
                .height =
                    static_cast<float>(height) * windowToLogical
            };
        }

        int pixelWidth = 0;
        int pixelHeight = 0;
        if (SDL_GetWindowSizeInPixels(
                record.window,
                &pixelWidth,
                &pixelHeight))
        {
            configuration.geometry.framebufferSize = {
                .width = ToFramebufferDimension(pixelWidth),
                .height = ToFramebufferDimension(pixelHeight)
            };
        }

        const auto logicalWidth =
            configuration.geometry.logicalBounds.size.width;
        const auto logicalHeight =
            configuration.geometry.logicalBounds.size.height;
        const auto framebuffer =
            configuration.geometry.framebufferSize;
        const auto displayScale =
            SDL_GetWindowDisplayScale(record.window);
        const auto fallbackScale =
            displayScale > 0.0F ? displayScale : 1.0F;
        configuration.geometry.scale = {
            .x = logicalWidth > 0.0F && framebuffer.width > 0
                ? static_cast<float>(framebuffer.width) / logicalWidth
                : fallbackScale,
            .y = logicalHeight > 0.0F && framebuffer.height > 0
                ? static_cast<float>(framebuffer.height) / logicalHeight
                : fallbackScale
        };

        return configuration;
    }

    WindowCapabilities SDLDesktopState::QueryCapabilities(
        const WindowRecord& record) const noexcept
    {
        (void)record;
        const auto* driver = SDL_GetCurrentVideoDriver();
        const bool compositorPositionsWindows =
            driver == nullptr ||
            std::string_view(driver) != "wayland";
        return WindowCapabilities {
            .decorations = true,
            .resizing = true,
            .minimizing = true,
            .maximizing = true,
            .fullscreen = true,
            .positioning = compositorPositionsWindows,
            .visibility = true,
            .focus = true,
            .inputControl = true,
            .alwaysOnTop = compositorPositionsWindows,
            .parentWindows = true,
            .childWindows = false,
            .toolWindows = true,
            .dialogWindows = true,
            .independentAxisScale = false,
            .applicationModality = false
        };
    }

    WindowPlatformMutationResult SDLDesktopState::MutationResult(
        WindowRecord& record,
        WindowOperationStatus status)
    {
        record.configuration = QueryConfiguration(record);
        return WindowPlatformMutation {
            .status = status,
            .configuration = record.configuration
        };
    }

    void SDLDesktopState::PublishConfiguration(
        WindowRecord& record) noexcept
    {
        try
        {
            record.configuration = QueryConfiguration(record);
            std::scoped_lock sinkLock(windowSinkMutex);
            if (auto* sink = windowSink)
            {
                sink->OnPlatformConfigurationChanged(
                    record.id,
                    record.configuration);
            }
        }
        catch (...)
        {
            // SDL event translation crosses a noexcept backend boundary.
        }
    }

    void SDLDesktopState::DispatchEvent(
        const SDL_Event& event) noexcept
    {
        if (event.type == wakeEventType.load())
        {
            return;
        }

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                DispatchQuitRequest();
                break;

            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            case SDL_EVENT_WINDOW_DESTROYED:
                DispatchWindowEvent(event.window);
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                DispatchKeyboardEvent(event.key);
                break;
            case SDL_EVENT_TEXT_INPUT:
                DispatchTextEvent(event.text);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                DispatchPointerMovedEvent(event.motion);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                DispatchPointerButtonEvent(event.button);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                DispatchPointerWheelEvent(event.wheel);
                break;
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_CANCELED:
                DispatchTouchEvent(event.tfinger);
                break;

            case SDL_EVENT_KEYBOARD_ADDED:
            case SDL_EVENT_KEYBOARD_REMOVED:
            case SDL_EVENT_MOUSE_ADDED:
            case SDL_EVENT_MOUSE_REMOVED:
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
            case SDL_EVENT_GAMEPAD_REMAPPED:
                DispatchDeviceEvent(event);
                break;
            default:
                break;
        }
    }

    void SDLDesktopState::DispatchWindowEvent(
        const SDL_WindowEvent& event) noexcept
    {
        auto* record = FindWindow(event.windowID);
        if (record == nullptr)
        {
            return;
        }

        const auto id = record->id;
        if (event.type == SDL_EVENT_WINDOW_DESTROYED)
        {
            // The native window is already gone, but Tyrant parentage is
            // semantic rather than native ownership. Normalize and publish
            // every retained child before removing the dead parent record.
            DetachChildren(id);
            windowIds.erase(event.windowID);
            windows.erase(id);
            InvalidateRemovedInputTarget(id);
            std::scoped_lock sinkLock(windowSinkMutex);
            if (auto* sink = windowSink)
            {
                sink->OnPlatformClosed(
                    id,
                    WindowCloseReason::PlatformRequest);
            }
            return;
        }

        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            std::scoped_lock sinkLock(windowSinkMutex);
            auto* sink = windowSink;
            if (sink != nullptr &&
                !sink->OnPlatformCloseRequested(
                    id,
                    WindowCloseReason::UserRequest))
            {
                return;
            }

            record = FindWindow(id);
            if (record == nullptr)
            {
                return;
            }

            (void)sink;
            (void)DestroyNativeWindow(
                id,
                WindowCloseReason::UserRequest,
                true);
            return;
        }

        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            // The desktop may deliver balancing release events to another
            // target after focus changes, so discard transient state now.
            record->pointerModesSuspended = true;
            RefreshTargetPointerModes(id, true);
            RefreshTextInput(
                InputPlatformTarget::FromValue(id.Value()));
        }

        PublishConfiguration(*record);
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
        {
            record = FindWindow(id);
            if (record != nullptr &&
                record->configuration.inputEnabled)
            {
                record->pointerModesSuspended = false;
                RefreshTargetPointerModes(id, false);
                RefreshTextInput(
                    InputPlatformTarget::FromValue(id.Value()));
            }
        }
    }

    void SDLDesktopState::DispatchQuitRequest() noexcept
    {
        std::vector<WindowId> openWindows;
        openWindows.reserve(windows.size());
        for (const auto& [id, record] : windows)
        {
            (void)record;
            openWindows.emplace_back(id);
        }

        for (const auto id : openWindows)
        {
            auto* record = FindWindow(id);
            if (record == nullptr)
            {
                continue;
            }

            std::scoped_lock sinkLock(windowSinkMutex);
            auto* sink = windowSink;
            if (sink != nullptr &&
                !sink->OnPlatformCloseRequested(
                    id,
                    WindowCloseReason::UserRequest))
            {
                continue;
            }

            record = FindWindow(id);
            if (record == nullptr)
            {
                continue;
            }

            (void)sink;
            (void)DestroyNativeWindow(
                id,
                WindowCloseReason::UserRequest,
                true);
        }
    }

    std::vector<SDLDesktopState::InputContextRecord>
        SDLDesktopState::InputTargetsForWindow(
            SDL_WindowID window) const
    {
        std::vector<InputContextRecord> result;
        const auto* windowRecord = FindWindow(window);
        if (windowRecord == nullptr ||
            !windowRecord->configuration.inputEnabled)
        {
            return result;
        }

        const auto targetValue = windowRecord->id.Value();
        for (const auto& [id, context] : inputContexts)
        {
            (void)id;
            if (context.configuration.enabled &&
                context.target.Value() == targetValue)
            {
                result.emplace_back(context);
            }
        }
        return result;
    }

    void SDLDesktopState::RefreshTargetPointerModes(
        WindowId window,
        bool inputInvalidated) noexcept
    {
        auto* windowRecord = FindWindow(window);
        if (windowRecord == nullptr)
        {
            return;
        }

        const auto target =
            InputPlatformTarget::FromValue(window.Value());
        const bool targetActive =
            windowRecord->configuration.inputEnabled &&
            windowRecord->configuration.focused &&
            !windowRecord->pointerModesSuspended;

        bool wantsRelative = false;
        for (const auto& [id, context] : inputContexts)
        {
            (void)id;
            if (context.target == target &&
                context.configuration.enabled &&
                context.requestedRelativePointerMode &&
                targetActive)
            {
                wantsRelative = true;
                break;
            }
        }
        const bool relativeApplied =
            SDL_SetWindowRelativeMouseMode(
                windowRecord->window,
                wantsRelative);

        bool wantsCapture = false;
        for (const auto& [id, context] : inputContexts)
        {
            (void)id;
            if (!context.configuration.enabled ||
                !context.requestedCapture)
            {
                continue;
            }

            const auto* contextWindow = FindWindow(
                WindowId::FromValue(context.target.Value()));
            if (contextWindow != nullptr &&
                contextWindow->configuration.inputEnabled &&
                contextWindow->configuration.focused &&
                !contextWindow->pointerModesSuspended)
            {
                wantsCapture = true;
                break;
            }
        }
        const bool captureApplied =
            SDL_CaptureMouse(wantsCapture);

        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        for (auto& [id, context] : inputContexts)
        {
            if (context.target != target)
            {
                continue;
            }

            context.configuration.captured =
                targetActive &&
                context.configuration.enabled &&
                context.requestedCapture &&
                captureApplied;
            context.configuration.relativePointerMode =
                targetActive &&
                context.configuration.enabled &&
                context.requestedRelativePointerMode &&
                relativeApplied;
            if (sink != nullptr)
            {
                sink->OnTargetInputChanged(
                    id,
                    context.configuration,
                    inputInvalidated);
            }
        }
    }

    void SDLDesktopState::DispatchKeyboardEvent(
        const SDL_KeyboardEvent& event) noexcept
    {
        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        if (sink == nullptr)
        {
            return;
        }

        const auto targets = InputTargetsForWindow(event.windowID);
        const auto keyName = CopyString(SDL_GetKeyName(event.key));
        const auto fallbackName =
            CopyString(SDL_GetScancodeName(event.scancode));
        const auto logicalName =
            keyName.empty() ? fallbackName : keyName;

        for (const auto& context : targets)
        {
            sink->OnKeyboard(InputPlatformKeyboardEvent {
                .context = context.id,
                .device = MakeDeviceId(
                    DeviceDomain::Keyboard,
                    event.which),
                .physicalCode =
                    TranslatePhysicalKey(event.scancode),
                .logicalName = logicalName,
                .action = event.down
                    ? InputAction::Pressed
                    : InputAction::Released,
                .modifiers = TranslateModifiers(event.mod),
                .repeat = event.repeat
            });
        }
    }

    void SDLDesktopState::DispatchTextEvent(
        const SDL_TextInputEvent& event) noexcept
    {
        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        if (sink == nullptr)
        {
            return;
        }

        const auto targets = InputTargetsForWindow(event.windowID);
        const auto text = CopyString(event.text);
        for (const auto& context : targets)
        {
            sink->OnText(InputPlatformTextEvent {
                .context = context.id,
                .device = MakeDeviceId(DeviceDomain::Keyboard, 0),
                .text = text
            });
        }
    }

    void SDLDesktopState::DispatchPointerMovedEvent(
        const SDL_MouseMotionEvent& event) noexcept
    {
        if (event.which == SDL_TOUCH_MOUSEID)
        {
            return;
        }

        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        if (sink == nullptr)
        {
            return;
        }

        const auto domain = event.which == SDL_PEN_MOUSEID
            ? DeviceDomain::Pen
            : DeviceDomain::Pointer;
        const auto* window = FindWindow(event.windowID);
        if (window == nullptr)
        {
            return;
        }
        const auto toLogical =
            WindowToLogicalScale(window->window);
        const auto targets = InputTargetsForWindow(event.windowID);
        for (const auto& context : targets)
        {
            sink->OnPointerMoved(InputPlatformPointerMovedEvent {
                .context = context.id,
                .device = MakeDeviceId(
                    domain,
                    static_cast<std::uint64_t>(event.which)),
                .position = InputPoint {
                    .x = event.x * toLogical,
                    .y = event.y * toLogical
                },
                .delta = InputDelta {
                    .x = event.xrel * toLogical,
                    .y = event.yrel * toLogical
                },
                .relative =
                    context.configuration.relativePointerMode
            });
        }
    }

    void SDLDesktopState::DispatchPointerButtonEvent(
        const SDL_MouseButtonEvent& event) noexcept
    {
        if (event.which == SDL_TOUCH_MOUSEID)
        {
            return;
        }

        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        if (sink == nullptr)
        {
            return;
        }

        const auto domain = event.which == SDL_PEN_MOUSEID
            ? DeviceDomain::Pen
            : DeviceDomain::Pointer;
        const auto* window = FindWindow(event.windowID);
        if (window == nullptr)
        {
            return;
        }
        const auto toLogical =
            WindowToLogicalScale(window->window);
        const auto targets = InputTargetsForWindow(event.windowID);
        for (const auto& context : targets)
        {
            sink->OnPointerButton(InputPlatformPointerButtonEvent {
                .context = context.id,
                .device = MakeDeviceId(
                    domain,
                    static_cast<std::uint64_t>(event.which)),
                .button = TranslatePointerButton(event.button),
                .action = event.down
                    ? InputAction::Pressed
                    : InputAction::Released,
                .position = InputPoint {
                    .x = event.x * toLogical,
                    .y = event.y * toLogical
                }
            });
        }
    }

    void SDLDesktopState::DispatchPointerWheelEvent(
        const SDL_MouseWheelEvent& event) noexcept
    {
        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        if (sink == nullptr)
        {
            return;
        }

        const auto direction =
            event.direction == SDL_MOUSEWHEEL_FLIPPED
                ? -1.0
                : 1.0;
        const auto targets = InputTargetsForWindow(event.windowID);
        for (const auto& context : targets)
        {
            sink->OnPointerWheel(InputPlatformPointerWheelEvent {
                .context = context.id,
                .device = MakeDeviceId(
                    DeviceDomain::Pointer,
                    static_cast<std::uint64_t>(event.which)),
                .delta = InputDelta {
                    .x = event.x * direction,
                    .y = event.y * direction
                },
                .unit = InputWheelUnit::Lines
            });
        }
    }

    void SDLDesktopState::DispatchTouchEvent(
        const SDL_TouchFingerEvent& event) noexcept
    {
        std::scoped_lock sinkLock(inputSinkMutex);
        auto* sink = inputSink;
        if (sink == nullptr)
        {
            return;
        }

        const auto deviceId = MakeDeviceId(
            DeviceDomain::Touch,
            static_cast<std::uint64_t>(event.touchID));
        {
            bool known = false;
            {
                std::scoped_lock lock(mutex);
                known = devices.contains(deviceId);
            }
            if (!known)
            {
                AddOrUpdateDevice(
                    InputDeviceChangeKind::Connected,
                    DescribeTouch(event.touchID));
            }
        }

        const auto* window = FindWindow(event.windowID);
        if (window == nullptr)
        {
            return;
        }
        const auto logicalSize =
            window->configuration.geometry.logicalBounds.size;
        const auto targets = InputTargetsForWindow(event.windowID);
        for (const auto& context : targets)
        {
            sink->OnTouch(InputPlatformTouchEvent {
                .context = context.id,
                .device = deviceId,
                .contact = static_cast<std::uint64_t>(event.fingerID),
                .action = TranslateTouchAction(event.type),
                .position = InputPoint {
                    .x = static_cast<double>(event.x) *
                        logicalSize.width,
                    .y = static_cast<double>(event.y) *
                        logicalSize.height
                },
                .pressure = event.pressure
            });
        }
    }

    void SDLDesktopState::DispatchDeviceEvent(
        const SDL_Event& event) noexcept
    {
        switch (event.type)
        {
            case SDL_EVENT_KEYBOARD_ADDED:
                AddOrUpdateDevice(
                    InputDeviceChangeKind::Connected,
                    DescribeKeyboard(event.kdevice.which));
                break;
            case SDL_EVENT_KEYBOARD_REMOVED:
                RemoveDevice(MakeDeviceId(
                    DeviceDomain::Keyboard,
                    event.kdevice.which));
                break;
            case SDL_EVENT_MOUSE_ADDED:
                AddOrUpdateDevice(
                    InputDeviceChangeKind::Connected,
                    DescribePointer(event.mdevice.which));
                break;
            case SDL_EVENT_MOUSE_REMOVED:
                RemoveDevice(MakeDeviceId(
                    DeviceDomain::Pointer,
                    event.mdevice.which));
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                AddOrUpdateDevice(
                    InputDeviceChangeKind::Connected,
                    DescribeGamepad(event.gdevice.which));
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                RemoveDevice(MakeDeviceId(
                    DeviceDomain::Gamepad,
                    event.gdevice.which));
                break;
            case SDL_EVENT_GAMEPAD_REMAPPED:
                AddOrUpdateDevice(
                    InputDeviceChangeKind::Updated,
                    DescribeGamepad(event.gdevice.which));
                break;
            default:
                break;
        }
    }

    std::vector<InputDeviceDescriptor>
        SDLDesktopState::ConnectedDevices() const
    {
        std::scoped_lock lock(mutex);
        std::vector<InputDeviceDescriptor> result;
        result.reserve(deviceOrder.size());
        for (const auto id : deviceOrder)
        {
            const auto found = devices.find(id);
            if (found != devices.end())
            {
                result.emplace_back(found->second);
            }
        }
        return result;
    }

    InputPlatformCreateResult SDLDesktopState::CreateInputContext(
        InputContextId id,
        const InputContextDescriptor& descriptor,
        InputPlatformTarget target)
    {
        if (!started.load())
        {
            return std::unexpected(InputError {
                .code = InputErrorCode::InvalidState,
                .message =
                    "SDL desktop services have not been started."
            });
        }
        if (!id || inputContexts.contains(id))
        {
            return std::unexpected(InputError {
                .code = InputErrorCode::InvalidDescriptor,
                .message =
                    "The input context identifier is invalid or in use."
            });
        }

        auto* targetWindow = ResolveTarget(target);
        if (target && targetWindow == nullptr)
        {
            return std::unexpected(InputError {
                .code = InputErrorCode::InvalidState,
                .message = std::format(
                    "Input target {} does not identify a live Tyrant window.",
                    target.Value())
            });
        }

        const bool windowBound = targetWindow != nullptr;
        InputContextRecord record {
            .id = id,
            .target = target,
            .configuration = InputContextConfiguration {
                .name = descriptor.name,
                .enabled = descriptor.initiallyEnabled,
                .captured = false,
                .relativePointerMode = false
            },
            .capabilities = InputContextCapabilities {
                .enableControl = true,
                .capture = windowBound,
                .relativePointerMode = windowBound,
                .keyboard = windowBound,
                .text = windowBound,
                .pointer = windowBound,
                .touch = windowBound
            },
            .requestedCapture = descriptor.requestCapture,
            .requestedRelativePointerMode =
                descriptor.requestRelativePointerMode
        };

        const auto [inserted, didInsert] =
            inputContexts.emplace(id, std::move(record));
        if (!didInsert)
        {
            return std::unexpected(InputError {
                .code = InputErrorCode::InvalidState,
                .message = "The SDL input identity map rejected a context."
            });
        }
        if (targetWindow != nullptr)
        {
            RefreshTargetPointerModes(
                WindowId::FromValue(target.Value()),
                false);
        }
        RefreshTextInput(target);

        return InputPlatformContextState {
            .configuration = inserted->second.configuration,
            .capabilities = inserted->second.capabilities
        };
    }

    InputOperationResult SDLDesktopState::DestroyInputContext(
        InputContextId id)
    {
        const auto found = inputContexts.find(id);
        if (found == inputContexts.end())
        {
            return std::unexpected(MissingContext(id));
        }

        const auto target = found->second.target;
        inputContexts.erase(found);
        if (ResolveTarget(target) != nullptr)
        {
            RefreshTargetPointerModes(
                WindowId::FromValue(target.Value()),
                false);
        }
        RefreshTextInput(target);
        return InputOperationStatus::Applied;
    }

    InputPlatformMutationResult SDLDesktopState::SetContextEnabled(
        InputContextId id,
        bool enabled)
    {
        const auto found = inputContexts.find(id);
        if (found == inputContexts.end())
        {
            return std::unexpected(MissingContext(id));
        }

        auto& record = found->second;
        record.configuration.enabled = enabled;
        if (ResolveTarget(record.target) != nullptr)
        {
            RefreshTargetPointerModes(
                WindowId::FromValue(record.target.Value()),
                false);
        }
        RefreshTextInput(record.target);
        return InputPlatformMutation {
            .status = InputOperationStatus::Applied,
            .configuration = record.configuration
        };
    }

    InputPlatformMutationResult SDLDesktopState::SetContextCapture(
        InputContextId id,
        bool captured)
    {
        const auto found = inputContexts.find(id);
        if (found == inputContexts.end())
        {
            return std::unexpected(MissingContext(id));
        }

        auto& record = found->second;
        record.requestedCapture = captured;
        auto* window = ResolveTarget(record.target);
        if (!record.capabilities.capture ||
            !record.configuration.enabled ||
            window == nullptr)
        {
            record.configuration.captured = false;
            return InputPlatformMutation {
                .status = captured
                    ? InputOperationStatus::Normalized
                    : InputOperationStatus::Applied,
                .configuration = record.configuration
            };
        }

        RefreshTargetPointerModes(
            WindowId::FromValue(record.target.Value()),
            false);
        return InputPlatformMutation {
            .status =
                record.configuration.captured == captured
                    ? InputOperationStatus::Applied
                    : InputOperationStatus::Normalized,
            .configuration = record.configuration
        };
    }

    InputPlatformMutationResult
        SDLDesktopState::SetRelativePointerMode(
            InputContextId id,
            bool enabled)
    {
        const auto found = inputContexts.find(id);
        if (found == inputContexts.end())
        {
            return std::unexpected(MissingContext(id));
        }

        auto& record = found->second;
        record.requestedRelativePointerMode = enabled;
        auto* window = ResolveTarget(record.target);
        if (!record.capabilities.relativePointerMode ||
            !record.configuration.enabled ||
            window == nullptr)
        {
            record.configuration.relativePointerMode = false;
            return InputPlatformMutation {
                .status = enabled
                    ? InputOperationStatus::Normalized
                    : InputOperationStatus::Applied,
                .configuration = record.configuration
            };
        }

        RefreshTargetPointerModes(
            WindowId::FromValue(record.target.Value()),
            false);
        return InputPlatformMutation {
            .status =
                record.configuration.relativePointerMode == enabled
                    ? InputOperationStatus::Applied
                    : InputOperationStatus::Normalized,
            .configuration = record.configuration
        };
    }

    void SDLDesktopState::ShutdownInput() noexcept
    {
        std::vector<InputPlatformTarget> targets;
        targets.reserve(inputContexts.size());
        for (const auto& [id, context] : inputContexts)
        {
            (void)id;
            if (context.target)
            {
                targets.emplace_back(context.target);
            }
        }
        std::ranges::sort(
            targets,
            {},
            &InputPlatformTarget::Value);
        targets.erase(
            std::unique(targets.begin(), targets.end()),
            targets.end());

        inputContexts.clear();
        (void)SDL_CaptureMouse(false);
        for (const auto target : targets)
        {
            if (auto* window = ResolveTarget(target))
            {
                (void)SDL_StopTextInput(window);
                (void)SDL_SetWindowRelativeMouseMode(window, false);
            }
        }
    }

    SDL_Window* SDLDesktopState::ResolveTarget(
        InputPlatformTarget target) const noexcept
    {
        if (!target)
        {
            return nullptr;
        }
        const auto* record = FindWindow(
            WindowId::FromValue(target.Value()));
        return record == nullptr ? nullptr : record->window;
    }

    void SDLDesktopState::RefreshTextInput(
        InputPlatformTarget target) noexcept
    {
        auto* window = ResolveTarget(target);
        if (window == nullptr)
        {
            return;
        }

        const auto* windowRecord = FindWindow(
            WindowId::FromValue(target.Value()));
        const bool targetActive =
            windowRecord != nullptr &&
            windowRecord->configuration.inputEnabled &&
            windowRecord->configuration.focused &&
            !windowRecord->pointerModesSuspended;
        bool needsText = false;
        for (const auto& [id, context] : inputContexts)
        {
            (void)id;
            if (targetActive &&
                context.target == target &&
                context.configuration.enabled &&
                context.capabilities.text)
            {
                needsText = true;
                break;
            }
        }

        if (needsText)
        {
            (void)SDL_StartTextInput(window);
        }
        else
        {
            (void)SDL_StopTextInput(window);
        }
    }

    InputDeviceId SDLDesktopState::MakeDeviceId(
        DeviceDomain domain,
        std::uint64_t nativeId) noexcept
    {
        constexpr std::uint64_t NativeMask =
            UINT64_C(0x00FFFFFFFFFFFFFF);
        const auto value =
            (static_cast<std::uint64_t>(domain) << 56U) |
            (nativeId & NativeMask);
        return InputDeviceId::FromValue(value);
    }

    InputDeviceDescriptor SDLDesktopState::DescribeKeyboard(
        SDL_KeyboardID id) const
    {
        auto name = CopyString(SDL_GetKeyboardNameForID(id));
        if (name.empty())
        {
            name = id == 0
                ? "Default keyboard"
                : std::format("Keyboard {}", id);
        }
        return InputDeviceDescriptor {
            .id = MakeDeviceId(DeviceDomain::Keyboard, id),
            .kind = InputDeviceKind::Keyboard,
            .name = std::move(name),
            .vendorId = std::nullopt,
            .productId = std::nullopt,
            .virtualDevice = id == 0
        };
    }

    InputDeviceDescriptor SDLDesktopState::DescribePointer(
        SDL_MouseID id) const
    {
        auto name = CopyString(SDL_GetMouseNameForID(id));
        if (name.empty())
        {
            name = id == 0
                ? "Default pointer"
                : std::format("Pointer {}", id);
        }
        return InputDeviceDescriptor {
            .id = MakeDeviceId(DeviceDomain::Pointer, id),
            .kind = InputDeviceKind::Pointer,
            .name = std::move(name),
            .vendorId = std::nullopt,
            .productId = std::nullopt,
            .virtualDevice = id == 0
        };
    }

    InputDeviceDescriptor SDLDesktopState::DescribeTouch(
        SDL_TouchID id) const
    {
        auto name = CopyString(SDL_GetTouchDeviceName(id));
        if (name.empty())
        {
            name = std::format(
                "Touch device {}",
                static_cast<std::uint64_t>(id));
        }
        return InputDeviceDescriptor {
            .id = MakeDeviceId(
                DeviceDomain::Touch,
                static_cast<std::uint64_t>(id)),
            .kind = InputDeviceKind::Touch,
            .name = std::move(name),
            .vendorId = std::nullopt,
            .productId = std::nullopt,
            .virtualDevice = false
        };
    }

    InputDeviceDescriptor SDLDesktopState::DescribeGamepad(
        SDL_JoystickID id) const
    {
        auto name = CopyString(SDL_GetGamepadNameForID(id));
        if (name.empty())
        {
            name = std::format("Gamepad {}", id);
        }
        return InputDeviceDescriptor {
            .id = MakeDeviceId(DeviceDomain::Gamepad, id),
            .kind = InputDeviceKind::Gamepad,
            .name = std::move(name),
            .vendorId = OptionalIdentifier(
                SDL_GetGamepadVendorForID(id)),
            .productId = OptionalIdentifier(
                SDL_GetGamepadProductForID(id)),
            .virtualDevice = false
        };
    }

    void SDLDesktopState::RefreshConnectedDevices()
    {
        int keyboardCount = 0;
        auto* keyboards = SDL_GetKeyboards(&keyboardCount);
        for (int index = 0; index < keyboardCount; ++index)
        {
            AddOrUpdateDevice(
                InputDeviceChangeKind::Connected,
                DescribeKeyboard(keyboards[index]));
        }
        SDL_free(keyboards);

        int pointerCount = 0;
        auto* pointers = SDL_GetMice(&pointerCount);
        for (int index = 0; index < pointerCount; ++index)
        {
            AddOrUpdateDevice(
                InputDeviceChangeKind::Connected,
                DescribePointer(pointers[index]));
        }
        SDL_free(pointers);

        int touchCount = 0;
        auto* touchDevices = SDL_GetTouchDevices(&touchCount);
        for (int index = 0; index < touchCount; ++index)
        {
            AddOrUpdateDevice(
                InputDeviceChangeKind::Connected,
                DescribeTouch(touchDevices[index]));
        }
        SDL_free(touchDevices);

        int gamepadCount = 0;
        auto* gamepads = SDL_GetGamepads(&gamepadCount);
        for (int index = 0; index < gamepadCount; ++index)
        {
            AddOrUpdateDevice(
                InputDeviceChangeKind::Connected,
                DescribeGamepad(gamepads[index]));
        }
        SDL_free(gamepads);
    }

    void SDLDesktopState::AddOrUpdateDevice(
        InputDeviceChangeKind change,
        InputDeviceDescriptor device) noexcept
    {
        try
        {
            InputDeviceChangeKind effectiveChange = change;
            {
                std::scoped_lock lock(mutex);
                const auto found = devices.find(device.id);
                if (found == devices.end())
                {
                    devices.emplace(device.id, device);
                    deviceOrder.emplace_back(device.id);
                    effectiveChange = InputDeviceChangeKind::Connected;
                }
                else
                {
                    if (change == InputDeviceChangeKind::Connected &&
                        found->second == device)
                    {
                        return;
                    }
                    found->second = device;
                    effectiveChange = InputDeviceChangeKind::Updated;
                }
            }

            std::scoped_lock sinkLock(inputSinkMutex);
            if (auto* sink = inputSink)
            {
                sink->OnDeviceChanged(
                    effectiveChange,
                    std::move(device));
            }
        }
        catch (...)
        {
            // Device discovery must not tear down the desktop pump.
        }
    }

    void SDLDesktopState::RemoveDevice(InputDeviceId id) noexcept
    {
        try
        {
            std::optional<InputDeviceDescriptor> removed;
            {
                std::scoped_lock lock(mutex);
                const auto found = devices.find(id);
                if (found == devices.end())
                {
                    return;
                }
                removed = std::move(found->second);
                devices.erase(found);
                std::erase(deviceOrder, id);
            }

            if (removed)
            {
                std::scoped_lock sinkLock(inputSinkMutex);
                if (auto* sink = inputSink)
                {
                    sink->OnDeviceChanged(
                        InputDeviceChangeKind::Disconnected,
                        std::move(*removed));
                }
            }
        }
        catch (...)
        {
            // See AddOrUpdateDevice.
        }
    }

    InputModifiers SDLDesktopState::TranslateModifiers(
        SDL_Keymod modifiers) noexcept
    {
        return InputModifiers {
            .shift = (modifiers & SDL_KMOD_SHIFT) != 0,
            .control = (modifiers & SDL_KMOD_CTRL) != 0,
            .alt = (modifiers & SDL_KMOD_ALT) != 0,
            .super = (modifiers & SDL_KMOD_GUI) != 0,
            .capsLock = (modifiers & SDL_KMOD_CAPS) != 0,
            .numLock = (modifiers & SDL_KMOD_NUM) != 0
        };
    }

    PhysicalKeyCode SDLDesktopState::TranslatePhysicalKey(
        SDL_Scancode scancode) noexcept
    {
        // SDL's standard keyboard scancodes in this range are explicitly
        // based on the USB HID keyboard usage page. Publishing the usage,
        // rather than an SDL enum, gives Tyrant a stable physical position.
        if (scancode >= SDL_SCANCODE_A &&
            scancode <= SDL_SCANCODE_RGUI)
        {
            return PhysicalKeyCode::FromValue(
                static_cast<std::uint32_t>(scancode));
        }
        return {};
    }

    PointerButton SDLDesktopState::TranslatePointerButton(
        std::uint8_t button) noexcept
    {
        switch (button)
        {
            case SDL_BUTTON_LEFT:
                return PointerButton::Primary;
            case SDL_BUTTON_RIGHT:
                return PointerButton::Secondary;
            case SDL_BUTTON_MIDDLE:
                return PointerButton::Middle;
            case SDL_BUTTON_X1:
                return PointerButton::Auxiliary1;
            case SDL_BUTTON_X2:
                return PointerButton::Auxiliary2;
            default:
                return PointerButton::Other;
        }
    }

    TouchAction SDLDesktopState::TranslateTouchAction(
        SDL_EventType event) noexcept
    {
        switch (event)
        {
            case SDL_EVENT_FINGER_DOWN:
                return TouchAction::Began;
            case SDL_EVENT_FINGER_UP:
                return TouchAction::Ended;
            case SDL_EVENT_FINGER_CANCELED:
                return TouchAction::Cancelled;
            default:
                return TouchAction::Moved;
        }
    }

    WindowError SDLDesktopState::WindowFailure(
        std::string operation)
    {
        return WindowError {
            .code = WindowErrorCode::PlatformFailure,
            .message = std::format(
                "SDL could not {}: {}",
                std::move(operation),
                SDL_GetError())
        };
    }

    WindowError SDLDesktopState::MissingWindow(WindowId id)
    {
        return WindowError {
            .code = WindowErrorCode::WindowDestroyed,
            .message = std::format(
                "SDL window {} is not live.",
                id.Value())
        };
    }

    InputError SDLDesktopState::InputFailure(
        std::string operation)
    {
        return InputError {
            .code = InputErrorCode::PlatformFailure,
            .message = std::format(
                "SDL could not {}: {}",
                std::move(operation),
                SDL_GetError())
        };
    }

    InputError SDLDesktopState::MissingContext(InputContextId id)
    {
        return InputError {
            .code = InputErrorCode::ContextNotFound,
            .message = std::format(
                "SDL input context {} is not live.",
                id.Value())
        };
    }
}
