#pragma once

#include <memory>

#include "Internal/Graphics/IWindowPresenter.hpp"

namespace TGE::Internal
{
    /**
     * @brief Create the renderer-owned Vulkan window presenter.
     *
     * Vulkan initialization is deferred until the first Wayland window is
     * attached. Native handles remain borrowed from the presentation target
     * provider for the duration of each attachment.
     */
    [[nodiscard]] std::unique_ptr<IWindowPresenter>
        CreateVulkanWindowPresenter();
}
