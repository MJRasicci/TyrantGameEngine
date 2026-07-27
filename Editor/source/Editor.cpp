#include "Editor.hpp"

#include <format>
#include <utility>

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    EditorSettings,
    TGE::Inject<TGE::OptionsMonitor<EditorOptions>>(),
    TGE::Inject<TGE::IOptionsStore<EditorOptions>>());

TGE_DECLARE_SERVICE_DEPENDENCIES(
    Editor,
    TGE::Inject<TGE::Logger<Editor>>(),
    TGE::Inject<TGE::ApplicationLifetime>(),
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
    std::shared_ptr<TGE::IOptionsMonitor<EditorOptions>> options,
    std::shared_ptr<EditorSettings> settings)
    : logger(std::move(logger)),
      lifetime(std::move(lifetime)),
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

    // The editor has no event loop yet, so this placeholder host completes one
    // lifecycle immediately after proving startup succeeded.
    lifetime->RequestStop();
    co_return;
}

TGE::Task<void> Editor::StopAsync()
{
    optionsSubscription.Reset();
    logger->Info("Stopping Editor...");
    co_return;
}

int main()
{
    auto application = TGE::Application::Create();

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

    // Registering the provider above grants read access only. This separate
    // service registration explicitly gives settings UI code write authority
    // for this one fixed JSON file; environment overrides remain read-only.
    application.Services()
        .AddSingleton<TGE::IOptionsStore<EditorOptions>>(userSettings);
    application.Services().AddSingleton<EditorSettings>();

    application.Services().AddTransient<TGE::Logger<Editor>>();
    application.AddHostedService<Editor>();

    return application.Run();
}
