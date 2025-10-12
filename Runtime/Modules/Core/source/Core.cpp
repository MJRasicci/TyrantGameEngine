#include "Core.hpp"

#include <iostream>
#include <memory>
#include <utility>

namespace
{
    class ConsoleLogSink final : public ILogSink
    {
    public:
        void Log(const std::string& message) override
        {
            std::cout << message << std::endl;
        }
    };
}

Engine::Engine(std::shared_ptr<ILogSink> logSink)
    : m_logSink(std::move(logSink))
{
    if (!m_logSink)
    {
        m_logSink = std::make_shared<ConsoleLogSink>();
    }
}

void Engine::Startup()
{
    if (m_logSink)
    {
        m_logSink->Log("Engine Starting...");
    }
}
