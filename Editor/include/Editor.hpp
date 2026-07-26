#pragma once

#include "TGE/Core.hpp"

#include <memory>
#include <stop_token>

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
        std::shared_ptr<TGE::ApplicationLifetime> lifetime);

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
};
