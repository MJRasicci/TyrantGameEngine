#include "Editor.hpp"

#include <format>
#include <utility>

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    Editor,
    TGE::Inject<TGE::Logger<Editor>>(),
    TGE::Inject<TGE::ApplicationLifetime>(),
    TGE::Inject<TGE::IOptionsMonitor<EditorOptions>>());
#endif

Editor::Editor(
    std::shared_ptr<TGE::Logger<Editor>> logger,
    std::shared_ptr<TGE::ApplicationLifetime> lifetime,
    std::shared_ptr<TGE::IOptionsMonitor<EditorOptions>> options)
    : logger(std::move(logger)),
      lifetime(std::move(lifetime)),
      options(std::move(options))
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

    application.Services()
        .AddOptions<EditorOptions>()
        .FromJsonFile(
            "editor.options.json",
            {
                .optional = true,
                .reloadOnChange = true
            })
        .FromEnvironment("TGE_EDITOR")
        .Validate(
            [](const EditorOptions& options)
            {
                return !options.autosave_enabled ||
                    options.autosave_interval_seconds > 0;
            },
            "autosave_interval_seconds must be positive when autosave is enabled");

    application.Services().AddTransient<TGE::Logger<Editor>>();
    application.Services().AddHostedService<Editor>();

    return application.Run();
}
