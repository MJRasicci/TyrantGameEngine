#include <benchmark/benchmark.h>

#include "Core.hpp"

#include <memory>
#include <string>

namespace
{
    class NullLogSink final : public ILogSink
    {
    public:
        void Log(const std::string& /*message*/) override {}
    };
}

static void BenchmarkEngineStartup(benchmark::State& state)
{
    auto sink = std::make_shared<NullLogSink>();
    for (auto _ : state)
    {
        Engine engine(sink);
        engine.Startup();
        benchmark::DoNotOptimize(engine);
    }
}

BENCHMARK(BenchmarkEngineStartup);
