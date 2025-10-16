#pragma once

#include "Logging/Logger.hpp"
#include <memory>

class Editor
{
public:
    void Run()
    {
        auto globalLogger = std::make_shared<TGE::GlobalLogger>();
        auto logger = std::make_shared<TGE::Logger<Editor>>(globalLogger);
        logger->Info("Starting Editor...");
    }
};