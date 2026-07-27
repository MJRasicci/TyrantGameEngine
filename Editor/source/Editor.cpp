#include "Editor.hpp"

#include <format>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
    std::string_view ToString(TGE::InputAction action) noexcept
    {
        return action == TGE::InputAction::Pressed
            ? "pressed"
            : "released";
    }

    std::string_view ToString(TGE::PointerButton button) noexcept
    {
        switch (button)
        {
        case TGE::PointerButton::Primary:
            return "primary";
        case TGE::PointerButton::Secondary:
            return "secondary";
        case TGE::PointerButton::Middle:
            return "middle";
        case TGE::PointerButton::Auxiliary1:
            return "auxiliary-1";
        case TGE::PointerButton::Auxiliary2:
            return "auxiliary-2";
        case TGE::PointerButton::Other:
            return "other";
        }
        return "unknown";
    }

    std::string_view ToString(TGE::TouchAction action) noexcept
    {
        switch (action)
        {
        case TGE::TouchAction::Began:
            return "began";
        case TGE::TouchAction::Moved:
            return "moved";
        case TGE::TouchAction::Ended:
            return "ended";
        case TGE::TouchAction::Cancelled:
            return "cancelled";
        }
        return "unknown";
    }

    std::string_view ToString(TGE::WindowCloseReason reason) noexcept
    {
        switch (reason)
        {
        case TGE::WindowCloseReason::ApplicationRequest:
            return "application request";
        case TGE::WindowCloseReason::UserRequest:
            return "user request";
        case TGE::WindowCloseReason::ParentClosed:
            return "parent closed";
        case TGE::WindowCloseReason::PlatformRequest:
            return "platform request";
        }
        return "unknown";
    }

    EditorLaunchOptions ParseLaunchOptions(int argc, char** argv)
    {
        EditorLaunchOptions result;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--headless")
            {
                result.headless = true;
            }
            else if (argument == "--smoke")
            {
                result.closeAfterStartup = true;
            }
            else
            {
                throw std::invalid_argument(std::format(
                    "Unknown Editor argument: {}",
                    argument));
            }
        }
        return result;
    }
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    EditorSettings,
    TGE::Inject<TGE::OptionsMonitor<EditorOptions>>(),
    TGE::Inject<TGE::IOptionsStore<EditorOptions>>());

TGE_DECLARE_SERVICE_DEPENDENCIES(
    Editor,
    TGE::Inject<TGE::Logger<Editor>>(),
    TGE::Inject<TGE::ApplicationLifetime>(),
    TGE::Inject<TGE::GuiApplicationContext>(),
    TGE::Inject<EditorLaunchOptions>(),
    TGE::Inject<TGE::IOptionsMonitor<EditorOptions>>(),
    TGE::Inject<EditorSettings>());
#endif

EditorSettings::EditorSettings(
    std::shared_ptr<TGE::OptionsMonitor<EditorOptions>> options,
    std::shared_ptr<TGE::IOptionsStore<EditorOptions>> store)
    : options(std::move(options)),
      store(std::move(store))
{
}

TGE::OptionsResult<void> EditorSettings::Save(EditorOptions value)
{
    auto published = options->Set(value);
    if (!published)
    {
        return std::unexpected(std::move(published.error()));
    }
    return store->Save(value);
}

Editor::Editor(
    std::shared_ptr<TGE::Logger<Editor>> logger,
    std::shared_ptr<TGE::ApplicationLifetime> lifetime,
    std::shared_ptr<TGE::GuiApplicationContext> guiContext,
    std::shared_ptr<EditorLaunchOptions> launchOptions,
    std::shared_ptr<TGE::IOptionsMonitor<EditorOptions>> options,
    std::shared_ptr<EditorSettings> settings)
    : logger(std::move(logger)),
      lifetime(std::move(lifetime)),
      guiContext(std::move(guiContext)),
      launchOptions(std::move(launchOptions)),
      options(std::move(options)),
      settings(std::move(settings))
{
}

TGE::Task<void> Editor::StartAsync(std::stop_token)
{
    optionsSubscription = options->Observe(
        [logger = logger](const TGE::OptionsChange<EditorOptions>& change)
        {
            if (!change.previous)
            {
                logger->Info(std::format(
                    "Starting Editor for project \"{}\" "
                    "(options version {}).",
                    change.current->project_name,
                    change.version));
                return;
            }

            logger->Info(std::format(
                "Editor options updated to version {} for project \"{}\".",
                change.version,
                change.current->project_name));
        });

    if (launchOptions->headless)
    {
        logger->Info("Editor headless lifecycle smoke started.");
        lifetime->RequestStop();
        co_return;
    }

    const auto root = guiContext->RootSession();
    if (!root || !root->Window())
    {
        throw std::runtime_error(
            "The Editor requires a live GuiApplication root window.");
    }

    const auto window = root->Window();
    const auto input = root->InputContext();
    if (!input)
    {
        throw std::runtime_error(
            "The Editor root window has no routed input context.");
    }

    windowResizedSubscription = window->SubscribeResized(
        [logger = logger](const TGE::WindowResizedEvent& event)
        {
            const auto& logical = event.current.logicalBounds.size;
            const auto& framebuffer = event.current.framebufferSize;
            const auto& scale = event.current.scale;
            logger->Info(std::format(
                "Window {} resized: logical={}x{}, framebuffer={}x{}, "
                "scale={}x{}.",
                event.window.Value(),
                logical.width,
                logical.height,
                framebuffer.width,
                framebuffer.height,
                scale.x,
                scale.y));
        });
    windowClosedSubscription = window->SubscribeClosed(
        [logger = logger](const TGE::WindowClosedEvent& event)
        {
            logger->Info(std::format(
                "Window {} closed by {}.",
                event.window.Value(),
                ToString(event.reason)));
        });

    keyboardSubscription = input->SubscribeKeyboard(
        [logger = logger](const TGE::KeyboardInputEvent& event)
        {
            logger->Info(std::format(
                "Keyboard {}: context={}, device={}, physical={}, "
                "logical=\"{}\", repeat={}, modifiers=[shift={}, "
                "control={}, alt={}, super={}, caps-lock={}, num-lock={}].",
                ToString(event.action),
                event.metadata.context.Value(),
                event.metadata.device.Value(),
                event.physicalCode.Value(),
                event.logicalName,
                event.repeat,
                event.modifiers.shift,
                event.modifiers.control,
                event.modifiers.alt,
                event.modifiers.super,
                event.modifiers.capsLock,
                event.modifiers.numLock));
        });
    textSubscription = input->SubscribeText(
        [logger = logger](const TGE::TextInputEvent& event)
        {
            logger->Info(std::format(
                "Text input: context={}, device={}, text=\"{}\".",
                event.metadata.context.Value(),
                event.metadata.device.Value(),
                event.text));
        });
    pointerMovedSubscription = input->SubscribePointerMoved(
        [logger = logger](const TGE::PointerMovedEvent& event)
        {
            logger->Info(std::format(
                "Pointer moved: context={}, device={}, position=({}, {}), "
                "delta=({}, {}), relative={}.",
                event.metadata.context.Value(),
                event.metadata.device.Value(),
                event.position.x,
                event.position.y,
                event.delta.x,
                event.delta.y,
                event.relative));
        });
    pointerButtonSubscription = input->SubscribePointerButton(
        [logger = logger](const TGE::PointerButtonEvent& event)
        {
            logger->Info(std::format(
                "Pointer button {} {}: context={}, device={}, "
                "position=({}, {}).",
                ToString(event.button),
                ToString(event.action),
                event.metadata.context.Value(),
                event.metadata.device.Value(),
                event.position.x,
                event.position.y));
        });
    pointerWheelSubscription = input->SubscribePointerWheel(
        [logger = logger](const TGE::PointerWheelEvent& event)
        {
            logger->Info(std::format(
                "Pointer wheel: context={}, device={}, delta=({}, {}), "
                "unit={}.",
                event.metadata.context.Value(),
                event.metadata.device.Value(),
                event.delta.x,
                event.delta.y,
                event.unit == TGE::InputWheelUnit::Lines
                    ? "lines"
                    : "pixels"));
        });
    touchSubscription = input->SubscribeTouch(
        [logger = logger](const TGE::TouchInputEvent& event)
        {
            logger->Info(std::format(
                "Touch {}: context={}, device={}, contact={}, "
                "position=({}, {}), pressure={}.",
                ToString(event.action),
                event.metadata.context.Value(),
                event.metadata.device.Value(),
                event.contact,
                event.position.x,
                event.position.y,
                event.pressure));
        });

    logger->Info(std::format(
        "Editor root window {} is ready with input context {}.",
        window->Id().Value(),
        input->Id().Value()));

    if (launchOptions->closeAfterStartup)
    {
        const auto closed = co_await window->RequestCloseAsync();
        if (!closed)
        {
            throw std::runtime_error(std::format(
                "Editor smoke close failed: {}",
                closed.error().message));
        }
    }

    co_return;
}

TGE::Task<void> Editor::StopAsync()
{
    touchSubscription.Reset();
    pointerWheelSubscription.Reset();
    pointerButtonSubscription.Reset();
    pointerMovedSubscription.Reset();
    textSubscription.Reset();
    keyboardSubscription.Reset();
    windowClosedSubscription.Reset();
    windowResizedSubscription.Reset();
    optionsSubscription.Reset();
    logger->Info("Stopping Editor...");
    co_return;
}

int main(int argc, char** argv)
{
    try
    {
        auto launchOptions =
            std::make_shared<EditorLaunchOptions>(
                ParseLaunchOptions(argc, argv));
        auto application = TGE::GuiApplication::Create();

        if (!launchOptions->headless)
        {
            application.UseDefaultDesktopBackend()
                .ConfigureRootWindow(TGE::WindowDescriptor {
                    .title = "Tyrant Game Engine - Editor"
                });
        }

        auto userSettings =
            std::make_shared<TGE::JsonFileOptionsProvider<EditorOptions>>(
                "editor.options.json",
                TGE::JsonFileOptionsProviderSettings {
                    .optional = true,
                    .reloadOnChange = true
                });

        application.Services()
            .AddOptions<EditorOptions>()
            .AddProvider(userSettings)
            .FromEnvironment("TGE_EDITOR")
            .Validate(
                [](const EditorOptions& options)
                {
                    return !options.autosave_enabled ||
                        options.autosave_interval_seconds > 0;
                },
                "autosave_interval_seconds must be positive when autosave is enabled");

        // Registering the provider above grants read access only. This
        // separate registration gives settings UI code write authority for
        // this fixed JSON file; environment overrides remain read-only.
        application.Services()
            .AddSingleton<TGE::IOptionsStore<EditorOptions>>(
                userSettings);
        application.Services().AddSingleton(launchOptions);
        application.Services().AddSingleton<EditorSettings>();

        application.Services().AddTransient<TGE::Logger<Editor>>();
        application.AddHostedService<Editor>();

        return application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Editor failed: " << exception.what() << '\n';
        return 1;
    }
}
