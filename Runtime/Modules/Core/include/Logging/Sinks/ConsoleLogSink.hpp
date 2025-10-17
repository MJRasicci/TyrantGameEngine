#pragma once

#include "Logging/ILogSink.hpp"

#include <iostream>
#include <mutex>
#include <string_view>

namespace TGE {

/**
 * @brief Sink that writes formatted messages to a standard output stream.
 */
class ConsoleLogSink final : public ILogSink
{
public:
    /**
     * @brief Creates the sink.
     * @param outputStream Stream that receives log entries. Defaults to std::cout.
     */
    explicit ConsoleLogSink(std::ostream& outputStream = std::cout);

    /**
     * @brief Outputs the formatted message to the configured stream.
     */
    void Write(const LogMessage& message, std::string_view formattedMessage) override;

    /**
     * @brief Flushes the underlying stream to guarantee delivery.
     */
    void Flush() override;

private:
    std::ostream& stream;
    std::mutex writeMutex;
};

} // namespace TGE
