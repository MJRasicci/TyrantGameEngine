#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Core.hpp"

#include <memory>
#include <string>

namespace
{
    class MockLogSink : public ILogSink
    {
    public:
        MOCK_METHOD(void, Log, (const std::string& message), (override));
    };
}

TEST(EngineTests, StartupLogsOnceWhenSinkProvided)
{
    auto mockSink = std::make_shared<testing::StrictMock<MockLogSink>>();

    EXPECT_CALL(*mockSink, Log(testing::StrEq("Engine Starting..."))).Times(1);

    Engine engine(mockSink);

    ASSERT_NO_THROW(engine.Startup());
}
