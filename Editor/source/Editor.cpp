#include "TGE/Editor.hpp"

#include <stdexcept>

TGE_DECLARE_SERVICE_DEPENDENCIES(Editor, TGE::InjectLocator());

Editor::Editor(TGE::ServiceLocator& locator)
{
    auto* providerLocator = dynamic_cast<TGE::ServiceProvider*>(&locator);

    if (!providerLocator)
    {
        throw std::invalid_argument("Editor requires a ServiceProvider-backed locator.");
    }

    provider = providerLocator->shared_from_this();
}

void Editor::Run()
{
    Start();
}

void Editor::ConfigureServices(TGE::ServiceCollection& services)
{
    services.AddTransient<TGE::Logger<Editor>>();
}

void Editor::OnStart()
{
    logger = provider->GetRequiredService<TGE::Logger<Editor>>();
    logger->Info("Starting Editor...");
}

void Editor::OnStop()
{
    logger->Info("Stopping Editor...");
}

int main()
{
    TGE::ServiceCollection services;
    services.AddTransient<Editor>();

    auto provider = services.BuildServiceProvider();
    auto editor = provider->GetRequiredService<Editor>();
    editor->Run();
}
