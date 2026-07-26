#include <TGE/Core.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace
{
    struct ConsumerOptions
    {
        std::string endpoint { "localhost" };
        std::uint32_t workers { 4 };

        bool operator==(const ConsumerOptions&) const = default;
    };
}

int main()
{
    const ConsumerOptions expected {
        .endpoint = "installed-package",
        .workers = 8
    };

    auto serialized = TGE::SerializeOptions(expected);
    if (!serialized)
    {
        return 1;
    }

    auto deserialized =
        TGE::DeserializeOptions<ConsumerOptions>(*serialized);
    if (!deserialized || *deserialized != expected)
    {
        return 2;
    }

    TGE::ServiceCollection services;
    services.AddOptions<ConsumerOptions>()
        .Configure(
            [](ConsumerOptions& options)
            {
                options.endpoint = "resolved-through-ioc";
                options.workers = 12;
            });

    auto provider = services.BuildServiceProvider();
    auto monitor =
        provider->GetRequiredService<
            TGE::IOptionsMonitor<ConsumerOptions>>();
    const auto current = monitor->CurrentSnapshot();

    if (current->endpoint != "resolved-through-ioc" ||
        current->workers != 12 ||
        current.version != 1)
    {
        return 3;
    }

    return 0;
}
