#include "Editor.hpp"

#include <stdexcept>

#include "Services/ServiceCollection.hpp"
#include "Services/ServiceLocator.hpp"
#include "Services/ServiceProvider.hpp"
#include "Services/ServiceTraits.hpp"

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
    globalLogger = provider->GetRequiredService<TGE::GlobalLogger>();
    logger = provider->GetRequiredService<TGE::Logger<Editor>>();
    logger->Info("Starting Editor...");
}

void Editor::OnStop()
{
    if (globalLogger)
    {
        globalLogger->Flush();
    }
}

int main()
{
    TGE::ServiceCollection services;
    services.AddTransient<Editor>();

    auto provider = services.BuildServiceProvider();
    auto editor = provider->GetRequiredService<Editor>();
    editor->Run();
}
