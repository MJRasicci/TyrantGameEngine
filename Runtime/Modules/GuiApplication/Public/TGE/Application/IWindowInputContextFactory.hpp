/**
 * @file IWindowInputContextFactory.hpp
 * @brief Integrates window identity with backend-neutral input contexts.
 */

#pragma once

#include <memory>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Input/IInputManager.hpp"
#include "TGE/Input/InputTypes.hpp"

namespace TGE
{
    class IWindow;

    /**
     * @brief Backend integration point that binds Input to one engine window.
     *
     * Graphics owns window semantics and Input owns every keyboard, pointer,
     * touch, and device contract. A desktop backend implements this narrow
     * bridge using opaque engine identities; neither subsystem exposes native
     * window handles or backend event types.
     */
    class TGE_API IWindowInputContextFactory
    {
    public:
        virtual ~IWindowInputContextFactory();

        /**
         * @brief Create an input context routed to the supplied live window.
         */
        [[nodiscard]] virtual Task<InputContextResult>
            CreateForWindowAsync(
                std::shared_ptr<IWindow> window,
                InputContextDescriptor descriptor) = 0;
    };
}
