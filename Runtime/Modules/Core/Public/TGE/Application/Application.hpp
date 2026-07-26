/**
 * @file Application.hpp
 * @brief Concrete composition root and lifecycle owner for TGE applications.
 */

#pragma once

#include <memory>

#include "TGE/Application/ApplicationState.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"

namespace TGE
{
    class ServiceCollection;

    /**
     * @class Application
     * @brief Configures services and executes one application lifecycle.
     *
     * Application is intentionally concrete and single-use. Consumers compose
     * an application by registering ordinary and hosted services rather than
     * deriving from an engine host base class.
     */
    class TGE_API Application final
    {
    public:
        /**
         * @brief Construct an unconfigured application composition root.
         */
        Application();

        /**
         * @brief Request shutdown if execution is active and release owned state.
         */
        ~Application();

        /**
         * @brief Construct a new application composition root.
         */
        static Application Create();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /**
         * @brief Mutable service collection available before execution starts.
         * @throws std::logic_error after the application leaves Created state.
         */
        ServiceCollection& Services() &;

        /**
         * @brief Execute the complete lifecycle while blocking this thread.
         */
        int Run() &;

        /**
         * @brief Return the lazy asynchronous operation for the complete lifecycle.
         *
         * The Application must outlive the returned operation, and the operation
         * must be driven to completion.
         */
        Task<int> RunAsync() &;

        /**
         * @brief Request cooperative shutdown from any thread.
         */
        bool RequestStop(int exitCode = 0) noexcept;

        /**
         * @brief Current lifecycle state.
         */
        ApplicationState GetState() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
