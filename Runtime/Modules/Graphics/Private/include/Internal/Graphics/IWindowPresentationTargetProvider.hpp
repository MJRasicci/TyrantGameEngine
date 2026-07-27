#pragma once

#include <expected>
#include <variant>

#include "TGE/Graphics/WindowError.hpp"
#include "TGE/Graphics/WindowId.hpp"

namespace TGE::Internal
{
    /**
     * @brief Borrowed Wayland handles for renderer-owned presentation.
     *
     * The provider retains ownership of both handles. They remain valid until
     * the matching presentation-target invalidation callback.
     */
    struct WaylandWindowPresentationTarget
    {
        void* display { nullptr };
        void* surface { nullptr };
    };

    using WindowPresentationTarget =
        std::variant<std::monostate, WaylandWindowPresentationTarget>;
    using WindowPresentationTargetResult =
        std::expected<WindowPresentationTarget, WindowError>;

    /**
     * @brief Private native-target bridge keyed by Tyrant window identity.
     *
     * Calls are serialized on the desktop event runtime. A monostate target
     * means the active native backend needs no explicit presenter.
     */
    class IWindowPresentationTargetProvider
    {
    public:
        virtual ~IWindowPresentationTargetProvider() = default;

        [[nodiscard]] virtual WindowPresentationTargetResult
            GetPresentationTarget(WindowId id) = 0;
    };
}
