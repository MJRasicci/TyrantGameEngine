#include <benchmark/benchmark.h>

#include "Logging/GlobalLogger.hpp"
#include "Logging/LoggingOptions.hpp"

#include <memory>

namespace
{
    class NullSink final : public TGE::ILogSink
    {
    public:
        void Write(const TGE::LogMessage&, std::string_view) override {}
        void Flush() override {}
    };
}

static void BM_GlobalLoggerThroughput(benchmark::State& state)
{
    auto sink = std::make_shared<NullSink>();
    TGE::LoggingOptions options("{Message}\n");
    options.ClearSinks();
    options.AddSink(sink);

    auto logger = std::make_shared<TGE::GlobalLogger>(options);
    TGE::LogMessage message{ TGE::LogLevel::Info, "Benchmark", "Payload" };

    for (auto _ : state)
    {
        logger->Log(message);
    }
}

BENCHMARK(BM_GlobalLoggerThroughput);
