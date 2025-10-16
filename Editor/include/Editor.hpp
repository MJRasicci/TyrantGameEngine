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
        // If we don't do this, the app shuts down before buffers flush and log messages actually get written
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
};