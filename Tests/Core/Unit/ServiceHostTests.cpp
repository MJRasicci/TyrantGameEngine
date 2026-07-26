#include <gtest/gtest.h>

#include "TGE/Services/ServiceHost.hpp"

namespace
{
    class TestServiceHost final : public TGE::ServiceHost
    {
    public:
        bool started = false;
        bool stopped = false;

    protected:
        void ConfigureServices(TGE::ServiceCollection&) override
        {
        }

        void OnStart() override
        {
            started = true;
        }

        void OnStop() override
        {
            stopped = true;
        }
    };
}

TEST(ServiceHostTests, BuildsProviderAndRunsLifecycle)
{
    TestServiceHost host;

    EXPECT_NO_THROW(host.Start());
    EXPECT_TRUE(host.started);
    EXPECT_TRUE(host.stopped);
}
