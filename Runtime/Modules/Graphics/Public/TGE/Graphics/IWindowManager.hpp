#pragma once

#include <expected>
#include <memory>
#include <vector>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Graphics/WindowDescriptor.hpp"
#include "TGE/Graphics/WindowError.hpp"

namespace TGE
{
    class IWindow;

    using WindowResult = std::expected<std::shared_ptr<IWindow>, WindowError>;

    /**
     * @brief DI-registerable owner and directory of application windows.
     *
     * Implementations own every created window until it is destroyed. Returned
     * shared pointers are stable references, but do not extend the lifetime of
     * the underlying native window after DestroyWindow succeeds.
     */
    class TGE_API IWindowManager
    {
    public:
        virtual ~IWindowManager();

        [[nodiscard]] virtual Task<WindowResult> CreateWindowAsync(
            WindowDescriptor descriptor) = 0;

        [[nodiscard]] virtual std::shared_ptr<IWindow> FindWindow(
            WindowId id) const noexcept = 0;

        /**
         * @brief Snapshot of the manager's windows in creation order.
         */
        [[nodiscard]] virtual std::vector<std::shared_ptr<IWindow>>
            Windows() const = 0;

        /**
         * @brief Unconditionally destroy a window and its native resources.
         *
         * Successful completion leaves any externally retained IWindow facade
         * in its terminal Destroyed state.
         */
        virtual Task<WindowOperationResult> DestroyWindowAsync(
            WindowId id) = 0;
    };
}
