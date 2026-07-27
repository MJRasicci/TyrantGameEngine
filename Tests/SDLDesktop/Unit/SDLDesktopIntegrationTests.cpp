#include <cstdlib>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Application/GuiApplication.hpp"
#include "TGE/Application/GuiApplicationContext.hpp"
#include "TGE/Application/IHostedService.hpp"
#include "TGE/Application/WindowSession.hpp"
#include "TGE/Graphics/IWindow.hpp"
#include "TGE/Graphics/IWindowManager.hpp"
#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceTraits.hpp"

namespace
{
    struct SDLParentProbeState
    {
        void Fail(std::string message)
        {
            std::scoped_lock lock(mutex);
            failure = std::move(message);
        }

        void Succeed()
        {
            std::scoped_lock lock(mutex);
            modalSurvived = true;
        }

        mutable std::mutex mutex;
        std::string failure;
        bool modalSurvived { false };
    };

    class SDLParentProbe final : public TGE::IHostedService
    {
    public:
        SDLParentProbe(
            std::shared_ptr<TGE::IWindowManager> windows,
            std::shared_ptr<TGE::ApplicationLifetime> lifetime,
            std::shared_ptr<TGE::GuiApplicationContext> guiContext,
            std::shared_ptr<SDLParentProbeState> state)
            : windows(std::move(windows)),
              lifetime(std::move(lifetime)),
              guiContext(std::move(guiContext)),
              state(std::move(state))
        {
        }

        TGE::Task<void> StartAsync(std::stop_token) override
        {
            const auto rootSession = guiContext->RootSession();
            const auto root =
                rootSession ? rootSession->Window() : nullptr;
            if (!root)
            {
                state->Fail(
                    "The SDL integration root window was not available.");
                lifetime->RequestStop(1);
                co_return;
            }

            auto parentResult = co_await windows->CreateWindowAsync(
                TGE::WindowDescriptor {
                    .title = "Parent",
                    .role = TGE::WindowRole::Tool,
                    .parent = root->Id(),
                    .initiallyVisible = false
                });
            if (!parentResult)
            {
                state->Fail(parentResult.error().message);
                lifetime->RequestStop(1);
                co_return;
            }
            const auto parent = std::move(*parentResult);

            auto modalResult = co_await windows->CreateWindowAsync(
                TGE::WindowDescriptor {
                    .title = "Modal child",
                    .role = TGE::WindowRole::Modal,
                    .parent = parent->Id(),
                    .modality =
                        TGE::WindowModality::DisableParentTree,
                    .initiallyVisible = false
                });
            if (!modalResult)
            {
                state->Fail(modalResult.error().message);
                lifetime->RequestStop(1);
                co_return;
            }
            const auto modal = std::move(*modalResult);

            if (root->IsInputEnabled() ||
                parent->IsInputEnabled())
            {
                state->Fail(
                    "The nested modal did not suppress its parent tree.");
                lifetime->RequestStop(1);
                co_return;
            }

            const auto destroyed =
                co_await windows->DestroyWindowAsync(parent->Id());
            if (!destroyed)
            {
                state->Fail(destroyed.error().message);
                lifetime->RequestStop(1);
                co_return;
            }

            const auto renamed =
                co_await modal->SetTitleAsync("Detached modal");
            const auto effective =
                modal->EffectiveConfiguration();
            if (!renamed ||
                modal->LifecycleState() !=
                    TGE::WindowLifecycleState::Open ||
                modal->Title() != "Detached modal" ||
                effective.parent ||
                effective.modality !=
                    TGE::WindowModality::Modeless ||
                !root->IsInputEnabled())
            {
                state->Fail(
                    renamed
                        ? "The retained modal was not normalized and usable "
                          "after its parent closed."
                        : renamed.error().message);
                lifetime->RequestStop(1);
                co_return;
            }

            state->Succeed();
            (void)co_await windows->DestroyWindowAsync(modal->Id());
            lifetime->RequestStop();
        }

        TGE::Task<void> StopAsync() override
        {
            co_return;
        }

    private:
        std::shared_ptr<TGE::IWindowManager> windows;
        std::shared_ptr<TGE::ApplicationLifetime> lifetime;
        std::shared_ptr<TGE::GuiApplicationContext> guiContext;
        std::shared_ptr<SDLParentProbeState> state;
    };
}

#if !TGE_HAS_REFLECTION_DI
TGE_DECLARE_SERVICE_DEPENDENCIES(
    SDLParentProbe,
    TGE::Inject<TGE::IWindowManager>(),
    TGE::Inject<TGE::ApplicationLifetime>(),
    TGE::Inject<TGE::GuiApplicationContext>(),
    TGE::Inject<SDLParentProbeState>());
#endif

TEST(
    SDLDesktopIntegration,
    DestroyingAParentNormalizesItsRetainedModalAndReleasesSuppression)
{
#if defined(_WIN32)
    ASSERT_EQ(_putenv_s("SDL_VIDEODRIVER", "dummy"), 0);
#else
    ASSERT_EQ(setenv("SDL_VIDEODRIVER", "dummy", 1), 0);
#endif

    auto state = std::make_shared<SDLParentProbeState>();
    auto application = TGE::GuiApplication::Create();
    application.UseDefaultDesktopBackend();
    application.ConfigureRootWindow(TGE::WindowDescriptor {
        .title = "SDL integration root",
        .initiallyVisible = false
    });
    application.Services().AddSingleton<SDLParentProbeState>(state);
    application.AddHostedService<SDLParentProbe>();

    EXPECT_EQ(application.Run(), 0);
    std::scoped_lock lock(state->mutex);
    EXPECT_TRUE(state->failure.empty()) << state->failure;
    EXPECT_TRUE(state->modalSurvived);
}
