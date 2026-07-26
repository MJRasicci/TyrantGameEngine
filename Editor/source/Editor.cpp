#include "Editor.hpp"

#include <utility>

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    Editor,
    TGE::Inject<TGE::Logger<Editor>>(),
    TGE::Inject<TGE::ApplicationLifetime>());
#endif

Editor::Editor(
    std::shared_ptr<TGE::Logger<Editor>> logger,
    std::shared_ptr<TGE::ApplicationLifetime> lifetime)
    : logger(std::move(logger)),
      lifetime(std::move(lifetime))
{
}

TGE::Task<void> Editor::StartAsync(std::stop_token)
{
    logger->Info("Starting Editor...");

    // The editor has no event loop yet, so this placeholder host completes one
    // lifecycle immediately after proving startup succeeded.
    lifetime->RequestStop();
    co_return;
}

TGE::Task<void> Editor::StopAsync()
{
    logger->Info("Stopping Editor...");
    co_return;
}

int main()
{
    auto application = TGE::Application::Create();

    application.Services().AddTransient<TGE::Logger<Editor>>();
    application.Services().AddHostedService<Editor>();

    return application.Run();
}
