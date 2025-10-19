#pragma once

#include "TGE/Logging/GlobalLogger.hpp"
#include "TGE/Logging/Logger.hpp"
#include "TGE/Logging/LoggingOptions.hpp"

#include <memory>

/**
 * @brief Entry point for running the Tyrant editor application.
 */
class Editor
{
public:
    Editor();

    /**
     * @brief Executes the editor main loop.
     */
    void Run();

private:
    std::shared_ptr<TGE::GlobalLogger> globalLogger;
    std::shared_ptr<TGE::Logger<Editor>> logger;
};
