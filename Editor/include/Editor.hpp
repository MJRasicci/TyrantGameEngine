#pragma once

#include "TGE/Core.hpp"

namespace TGE
{
    class ServiceCollection;
    class ServiceLocator;
}

#include <memory>

/**
 * @brief Entry point for running the Tyrant editor application.
 */
class Editor : public TGE::ServiceHost
{
public:
    /**
     * @brief Builds the editor using services resolved from the host provider.
     * @param locator Root service locator supplied by the application container.
     */
    explicit Editor(TGE::ServiceLocator& locator);

    /**
     * @brief Executes the editor main loop.
     */
    void Run();

protected:
    /**
     * @brief Registers editor specific services with the dependency injection container.
     * @param services Mutable service registry used to configure the provider.
     */
    void ConfigureServices(TGE::ServiceCollection& services) override;

    /**
     * @brief Captures service references after the host provider has been constructed.
     */
    void OnStart() override;

    /**
     * @brief Ensures logging infrastructure drains buffered messages before shutdown.
     */
    void OnStop() override;

private:
    std::shared_ptr<TGE::Logger<Editor>> logger;
};
