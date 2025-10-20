#include "TGE/Logging/Sinks/FileLogSink.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>

namespace TGE {

FileLogSink::FileLogSink(std::filesystem::path filePath)
    : path(std::move(filePath)),
      file(path, std::ios::out | std::ios::app)
{
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open log file: " + path.string());
    }
}

void FileLogSink::Write(const LogMessage& /*message*/, std::string_view formattedMessage)
{
    std::scoped_lock guard(writeMutex);
    file << formattedMessage;
    file.flush();
}

void FileLogSink::Flush()
{
    std::scoped_lock guard(writeMutex);
    file.flush();
}

} // namespace TGE
