#pragma once

#include "TGE/Application.hpp"
#include "TGE/Core.hpp"

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
 * @brief Placeholder lifecycle service for the Tyrant editor.
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
    std::shared_ptr<TGE::IOptionsMonitor<EditorOptions>> options;
    std::shared_ptr<EditorSettings> settings;
    TGE::OptionsSubscription optionsSubscription;
};
