#include <TGE/Application.hpp>
#include <TGE/CliApplication.hpp>
#include <TGE/Core.hpp>
#include <TGE/Graphics.hpp>
#include <TGE/GuiApplication.hpp>
#include <TGE/Input.hpp>

#if TGE_INSTALLED_HAS_RENDERING
    #include <TGE/Rendering.hpp>
#endif
#if TGE_INSTALLED_HAS_DRAWING2D
    #include <TGE/Drawing2D.hpp>
#endif
#if TGE_INSTALLED_HAS_WORLD2D
    #include <TGE/World2D.hpp>
#endif

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

namespace TGE::Tests::InstalledConsumer
{
    struct ConsumerOptions
    {
        std::string endpoint { "localhost" };
        std::uint32_t workers { 4 };

        bool operator==(const ConsumerOptions&) const = default;
    };
}

using TGE::Tests::InstalledConsumer::ConsumerOptions;

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

#if TGE_INSTALLED_HAS_RENDERING
    auto image = TGE::Image::Create(
        { 2, 2 },
        TGE::PixelFormat::Rgba8Srgb);
    if (!image ||
        !image->Fill(TGE::Srgba8 { 255, 128, 0, 255 }))
    {
        return 9;
    }
#endif

#if TGE_INSTALLED_HAS_DRAWING2D
    const TGE::Transform2D drawingTransform {
        .translation = { 3.0F, 4.0F }
    };
    if (drawingTransform.Matrix().TransformPoint({}) !=
        TGE::Vector2f { 3.0F, 4.0F })
    {
        return 10;
    }
#endif

#if TGE_INSTALLED_HAS_WORLD2D
    TGE::World2D world;
    const auto node = world.CreateNode();
    if (!node)
    {
        return 11;
    }
    auto visual = world.AddRectangle(
        *node,
        TGE::Rectangle2D { .size = { 16.0F, 12.0F } });
    if (!visual || world.PublishRenderScene().Items().size() != 1)
    {
        return 12;
    }
#endif

    return 0;
}
