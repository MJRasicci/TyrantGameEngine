#include <TGE/Application.hpp>
#include <TGE/CliApplication.hpp>
#include <TGE/Core.hpp>
#include <TGE/Graphics.hpp>
#include <TGE/GuiApplication.hpp>
#include <TGE/Input.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>

static_assert(std::is_abstract_v<TGE::IWindow>);
static_assert(std::is_abstract_v<TGE::IWindowManager>);
static_assert(std::is_abstract_v<TGE::IInputContext>);
static_assert(std::is_abstract_v<TGE::IInputManager>);
static_assert(std::is_abstract_v<TGE::IWindowInputContextFactory>);

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
    const auto optionsPath =
        std::filesystem::temp_directory_path() /
        ("tge-installed-consumer-" +
         std::to_string(
             std::chrono::steady_clock::now()
                 .time_since_epoch()
                 .count()) +
         ".json");
    struct FileCleanup
    {
        ~FileCleanup()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }

        std::filesystem::path path;
    } cleanup { optionsPath };

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
    auto json =
        std::make_shared<TGE::JsonFileOptionsProvider<ConsumerOptions>>(
            optionsPath,
            TGE::JsonFileOptionsProviderSettings { .optional = true });
    services.AddOptions<ConsumerOptions>()
        .AddProvider(json)
        .Configure(
            [](ConsumerOptions& options)
            {
                options.endpoint = "resolved-through-ioc";
                options.workers = 12;
            });
    services.AddSingleton<TGE::IOptionsStore<ConsumerOptions>>(json);

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

    auto store =
        provider->GetRequiredService<
            TGE::IOptionsStore<ConsumerOptions>>();
    auto saved = store->Save(expected);
    if (!saved)
    {
        return 4;
    }

    ConsumerOptions persisted;
    auto applied = json->Apply(persisted);
    if (!applied || persisted != expected)
    {
        return 5;
    }

    auto application = TGE::Application::Create();
    application.RequestStop(6);
    if (application.Run() != 6)
    {
        return 6;
    }

    auto cli = TGE::CliApplication::Create();
    cli.RequestStop(7);
    if (cli.Run() != 7)
    {
        return 7;
    }

    auto gui = TGE::GuiApplication::Create();
    gui.RequestStop(8);
    if (gui.Run() != 8)
    {
        return 8;
    }

    return 0;
}
