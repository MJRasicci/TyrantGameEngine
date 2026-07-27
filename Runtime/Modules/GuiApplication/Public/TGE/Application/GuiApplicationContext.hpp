/**
 * @file GuiApplicationContext.hpp
 * @brief Thread-safe access to the live GUI application context.
 */

#pragma once

#include <memory>

#include "TGE/Export.hpp"

namespace TGE
{
    class WindowSession;

    namespace detail
    {
        class GuiApplicationCoordinator;
    }

    /**
     * @class GuiApplicationContext
     * @brief Injectable, stable accessor for GUI application state.
     *
     * GuiApplication registers one context instance before constructing its
     * service provider. The root session is published before user hosted
     * services start, remains available while they stop, and is cleared when
     * the GUI coordinator stops.
     */
    class TGE_API GuiApplicationContext final
    {
    public:
        GuiApplicationContext();
        ~GuiApplicationContext();

        GuiApplicationContext(const GuiApplicationContext&) = delete;
        GuiApplicationContext& operator=(const GuiApplicationContext&) =
            delete;
        GuiApplicationContext(GuiApplicationContext&&) = delete;
        GuiApplicationContext& operator=(GuiApplicationContext&&) = delete;

        /**
         * @brief Current root session, or null before creation/after teardown.
         *
         * This accessor may be called concurrently with application startup
         * and shutdown.
         */
        [[nodiscard]] std::shared_ptr<WindowSession> RootSession() const;

    private:
        friend class detail::GuiApplicationCoordinator;

        void PublishRootSession(std::shared_ptr<WindowSession> session);
        [[nodiscard]] std::shared_ptr<WindowSession> ClearRootSession();

        struct State;
        std::unique_ptr<State> state;
    };
}
