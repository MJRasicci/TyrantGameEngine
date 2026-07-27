#include "TGE/Application/GuiApplicationContext.hpp"

#include <mutex>
#include <utility>

#include "TGE/Application/WindowSession.hpp"

namespace TGE
{
    struct GuiApplicationContext::State
    {
        mutable std::mutex mutex;
        std::shared_ptr<WindowSession> rootSession;
    };

    GuiApplicationContext::GuiApplicationContext()
        : state(std::make_unique<State>())
    {
    }

    GuiApplicationContext::~GuiApplicationContext() = default;

    std::shared_ptr<WindowSession> GuiApplicationContext::RootSession() const
    {
        std::scoped_lock lock(state->mutex);
        return state->rootSession;
    }

    void GuiApplicationContext::PublishRootSession(
        std::shared_ptr<WindowSession> session)
    {
        std::scoped_lock lock(state->mutex);
        state->rootSession = std::move(session);
    }

    std::shared_ptr<WindowSession>
    GuiApplicationContext::ClearRootSession()
    {
        std::scoped_lock lock(state->mutex);
        return std::exchange(state->rootSession, {});
    }
}
