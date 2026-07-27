#pragma once

#include "TGE/Application.hpp"
#include "TGE/Core.hpp"
#include "TGE/Graphics.hpp"
#include "TGE/GuiApplication.hpp"
#include "TGE/Input.hpp"

#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>

/**
 * @brief Serializable, live configuration consumed by the editor service.
 */
struct EditorOptions
{
    std::string project_name { "Untitled" };
    bool autosave_enabled { true };
    std::uint32_t autosave_interval_seconds { 60 };
};

/**
 * @brief Process-local launch policy selected by the Editor executable.
 */
struct EditorLaunchOptions
{
    bool headless { false };
    bool closeAfterStartup { false };
};

/**
 * @brief Settings-menu authority for validated publication and persistence.
 */
class EditorSettings final
{
public:
    EditorSettings(
        std::shared_ptr<TGE::OptionsMonitor<EditorOptions>> options,
        std::shared_ptr<TGE::IOptionsStore<EditorOptions>> store);

    /**
     * @brief Validate, publish, and then persist one complete settings value.
     *
     * A persistence failure can leave a valid live-only value, which a real UI
     * should report and allow the user to retry.
     */
    [[nodiscard]] TGE::OptionsResult<void> Save(EditorOptions value);

private:
    std::shared_ptr<TGE::OptionsMonitor<EditorOptions>> options;
    std::shared_ptr<TGE::IOptionsStore<EditorOptions>> store;
};

/**
 * @brief Editor lifecycle service that observes its root window and input.
 */
class Editor final : public TGE::IHostedService
{
public:
    /**
     * @brief Construct the editor with application-managed dependencies.
     */
    Editor(
        std::shared_ptr<TGE::Logger<Editor>> logger,
        std::shared_ptr<TGE::ApplicationLifetime> lifetime,
        std::shared_ptr<TGE::GuiApplicationContext> guiContext,
        std::shared_ptr<EditorLaunchOptions> launchOptions,
        std::shared_ptr<TGE::IOptionsMonitor<EditorOptions>> options,
        std::shared_ptr<EditorSettings> settings);

    /**
     * @brief Start the editor service.
     */
    TGE::Task<void> StartAsync(std::stop_token stopping) override;

    /**
     * @brief Stop the editor service.
     */
    TGE::Task<void> StopAsync() override;

private:
    std::shared_ptr<TGE::Logger<Editor>> logger;
    std::shared_ptr<TGE::ApplicationLifetime> lifetime;
    std::shared_ptr<TGE::GuiApplicationContext> guiContext;
    std::shared_ptr<EditorLaunchOptions> launchOptions;
    std::shared_ptr<TGE::IOptionsMonitor<EditorOptions>> options;
    std::shared_ptr<EditorSettings> settings;
    TGE::OptionsSubscription optionsSubscription;
    TGE::WindowSubscription windowResizedSubscription;
    TGE::WindowSubscription windowClosedSubscription;
    TGE::InputSubscription keyboardSubscription;
    TGE::InputSubscription textSubscription;
    TGE::InputSubscription pointerMovedSubscription;
    TGE::InputSubscription pointerButtonSubscription;
    TGE::InputSubscription pointerWheelSubscription;
    TGE::InputSubscription touchSubscription;
};
