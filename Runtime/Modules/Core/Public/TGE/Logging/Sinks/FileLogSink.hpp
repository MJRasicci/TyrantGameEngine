#pragma once

#include "TGE/Logging/ILogSink.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace TGE {

/**
 * @brief Sink that appends log messages to a file on disk.
 */
class FileLogSink final : public ILogSink
{
public:
    /**
     * @brief Opens the provided file path for appending.
     * @param filePath Location of the log file on disk.
     */
    explicit FileLogSink(std::filesystem::path filePath);

    /**
     * @brief Appends the formatted message to the target file.
     */
    void Write(const LogMessage& message, std::string_view formattedMessage) override;

    /**
     * @brief Flushes buffered content to persistent storage.
     */
    void Flush() override;

private:
    std::filesystem::path path;
    std::ofstream file;
    std::mutex writeMutex;
};

} // namespace TGE
