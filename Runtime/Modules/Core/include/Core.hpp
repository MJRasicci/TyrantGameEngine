#pragma once

#include "Export.hpp"

#include <memory>
#include <string>

class TGE_API ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Log(const std::string& message) = 0;
};

class TGE_API Engine
{
public:
    explicit Engine(std::shared_ptr<ILogSink> logSink = nullptr);

    void Startup();

private:
    std::shared_ptr<ILogSink> m_logSink;
};
