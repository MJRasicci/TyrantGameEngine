#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Logging/GlobalLogger.hpp"
#include "Logging/LogFormatter.hpp"
#include "Logging/Logger.hpp"
#include "Logging/LoggingOptions.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::string StripAnsi(std::string_view input)
    {
        std::string output;
        output.reserve(input.size());

        for (size_t i = 0; i < input.size(); ++i)
        {
            if (input[i] == '\033')
            {
                while (i < input.size() && input[i] != 'm')
                {
                    ++i;
                }
                continue;
            }
            output.push_back(input[i]);
        }

        return output;
    }

    class CollectingSink final : public TGE::ILogSink
    {
    public:
        void Write(const TGE::LogMessage& message, std::string_view formattedMessage) override
        {
            std::scoped_lock guard(mutex);
            messages.emplace_back(message, std::string(formattedMessage));
            condition.notify_all();
        }

        void Flush() override
        {
            std::scoped_lock guard(mutex);
            condition.notify_all();
        }

        bool WaitForCount(std::size_t expected, std::chrono::milliseconds timeout)
        {
            std::unique_lock lock(mutex);
            return condition.wait_for(lock, timeout, [&]
            {
                return messages.size() >= expected;
            });
        }

        std::vector<std::pair<TGE::LogMessage, std::string>> messages;
        std::mutex mutex;
        std::condition_variable condition;
    };
}

TEST(LoggingOptionsTests, ProvidesConsoleSinkByDefault)
{
    TGE::LoggingOptions options;
    EXPECT_FALSE(options.GetSinks().empty());
}

TEST(LogFormatterTests, FormatsMessageWithCustomPattern)
{
    TGE::LogFormatter formatter("[{Level}] {SourceContext}: {Message}");
    TGE::LogMessage message{ TGE::LogLevel::Info, "FormatterTests", "Hello" };

    const auto formatted = StripAnsi(formatter.Format(message));
    EXPECT_THAT(formatted, testing::HasSubstr("[Info]"));
    EXPECT_THAT(formatted, testing::HasSubstr("FormatterTests"));
    EXPECT_THAT(formatted, testing::HasSubstr("Hello"));
}

TEST(GlobalLoggerTests, FlushesAllMessagesOnDestruction)
{
    auto sink = std::make_shared<CollectingSink>();
    TGE::LoggingOptions options("{Message}\n");
    options.ClearSinks();
    options.AddSink(sink);

    {
        TGE::GlobalLogger logger(options);
        logger.Log(TGE::LogMessage{ TGE::LogLevel::Info, "GlobalLoggerTests", "Queued message" });
    }

    EXPECT_TRUE(sink->WaitForCount(1, std::chrono::milliseconds(250)));
    ASSERT_EQ(sink->messages.size(), 1u);
    EXPECT_EQ(sink->messages.front().second, "Queued message\n");
}

TEST(LoggerTests, EmbedsTypeAsSourceContext)
{
    struct ContextType { };
    auto sink = std::make_shared<CollectingSink>();
    TGE::LoggingOptions options("{SourceContext}:{Message}\n");
    options.ClearSinks();
    options.AddSink(sink);

    auto globalLogger = std::make_shared<TGE::GlobalLogger>(options);
    TGE::Logger<ContextType> logger(globalLogger);

    logger.Info("Payload");
    EXPECT_TRUE(sink->WaitForCount(1, std::chrono::milliseconds(250)));
    const auto sanitized = StripAnsi(sink->messages.front().second);
    EXPECT_THAT(sanitized, testing::HasSubstr("ContextType"));
    EXPECT_THAT(sanitized, testing::HasSubstr("Payload"));
}
