#include "TGE/Logging/GlobalLogger.hpp"

#include <utility>

namespace TGE {

GlobalLogger::GlobalLogger()
    : GlobalLogger(LoggingOptions{})
{
}

GlobalLogger::GlobalLogger(LoggingOptions options)
    : stopRequested(false),
      options(std::move(options))
{
    worker = std::thread(&GlobalLogger::ProcessQueue, this);
}

GlobalLogger::~GlobalLogger()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopRequested = true;
    }
    condition.notify_all();

    if (worker.joinable())
    {
        worker.join();
    }

    Flush();
}

void GlobalLogger::Log(const LogMessage& message)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopRequested.load())
        {
            return;
        }
        messageQueue.push(message);
    }
    condition.notify_one();
}

void GlobalLogger::Flush()
{
    std::unique_lock<std::mutex> lock(queueMutex);
    condition.wait(lock, [this]
    {
        return messageQueue.empty() && !isDispatching;
    });

    lock.unlock();

    const auto& sinks = options.GetSinks();
    for (const auto& sink : sinks)
    {
        sink->Flush();
    }
}

void GlobalLogger::ProcessQueue()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this]
        {
            return stopRequested.load() || !messageQueue.empty();
        });

        if (messageQueue.empty() && stopRequested.load())
        {
            condition.notify_all();
            break;
        }

        auto message = messageQueue.front();
        messageQueue.pop();
        isDispatching = true;
        lock.unlock();

        Dispatch(message);

        lock.lock();
        isDispatching = false;
        condition.notify_all();
    }
}

void GlobalLogger::Dispatch(const LogMessage& message)
{
    const auto& sinks = options.GetSinks();
    auto formatted = options.GetFormatter().Format(message);

    for (const auto& sink : sinks)
    {
        sink->Write(message, formatted);
    }
}

} // namespace TGE
