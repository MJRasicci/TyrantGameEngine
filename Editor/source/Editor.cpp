#include "Editor.hpp"

Editor::Editor()
{
    TGE::LoggingOptions options;
    globalLogger = std::make_shared<TGE::GlobalLogger>(options);
    logger = std::make_shared<TGE::Logger<Editor>>(globalLogger);
}

void Editor::Run()
{
    logger->Info("Starting Editor...");
}

int main()
{
    Editor editor;
    editor.Run();
}
