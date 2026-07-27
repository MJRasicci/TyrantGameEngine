#include "SDLDesktopState.hpp"

#include <utility>

namespace TGE::Internal
{
    namespace
    {
        class SDLWindowPresentationTargetProvider final
            : public IWindowPresentationTargetProvider
        {
        public:
            explicit SDLWindowPresentationTargetProvider(
                std::shared_ptr<SDLDesktopState> state)
                : state(std::move(state))
            {
            }

            WindowPresentationTargetResult GetPresentationTarget(
                WindowId id) override
            {
                return state->GetPresentationTarget(id);
            }

        private:
            std::shared_ptr<SDLDesktopState> state;
        };
    }

    std::unique_ptr<IWindowPresentationTargetProvider>
        CreateSDLWindowPresentationTargetProvider(
            std::shared_ptr<SDLDesktopState> state)
    {
        return std::make_unique<
            SDLWindowPresentationTargetProvider>(
                std::move(state));
    }
}
