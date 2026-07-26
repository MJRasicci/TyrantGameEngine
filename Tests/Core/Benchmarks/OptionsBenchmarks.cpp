#include <benchmark/benchmark.h>

#include "TGE/Options/OptionsMonitor.hpp"
#include "TGE/Options/OptionsSerialization.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace
{
    struct BenchmarkOptions
    {
        std::string name { "Tyrant" };
        std::uint64_t generation {};
        bool enabled { true };
    };

    void OptionsCurrent(benchmark::State& state)
    {
        auto monitor =
            std::make_shared<TGE::OptionsMonitor<BenchmarkOptions>>();

        for (auto _ : state)
        {
            const auto snapshot = monitor->Current();
            benchmark::DoNotOptimize(snapshot.get());
            benchmark::DoNotOptimize(
                std::uint64_t(snapshot->generation));
        }
    }

    void OptionsPublish(benchmark::State& state)
    {
        auto monitor =
            std::make_shared<TGE::OptionsMonitor<BenchmarkOptions>>();
        std::uint64_t generation = 1;

        for (auto _ : state)
        {
            auto result = monitor->Set(BenchmarkOptions {
                .name = "Tyrant",
                .generation = generation++,
                .enabled = true
            });
            benchmark::DoNotOptimize(result);
        }
    }

    void OptionsDeserialize(benchmark::State& state)
    {
        constexpr std::string_view serialized =
            R"({"name":"Editor","generation":42,"enabled":false})";

        for (auto _ : state)
        {
            BenchmarkOptions options;
            auto result =
                TGE::DeserializeOptionsInto(options, serialized);
            benchmark::DoNotOptimize(result);
            benchmark::DoNotOptimize(
                std::uint64_t(options.generation));
        }
    }
}

BENCHMARK(OptionsCurrent);
BENCHMARK(OptionsPublish);
BENCHMARK(OptionsDeserialize);
