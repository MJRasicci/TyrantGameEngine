#include "TGE/Options/OptionsSubscription.hpp"

#include <utility>

namespace TGE
{
    struct OptionsSubscription::State
    {
        explicit State(std::function<void()> cancelAction)
            : cancel(std::move(cancelAction))
        {
        }

        ~State()
        {
            if (!cancel)
            {
                return;
            }

            try
            {
                cancel();
            }
            catch (...)
            {
                // Subscription destruction must never interrupt shutdown.
            }
        }

        std::function<void()> cancel;
    };

    OptionsSubscription::OptionsSubscription() noexcept = default;

    OptionsSubscription::OptionsSubscription(
        std::function<void()> cancelAction)
    {
        if (cancelAction)
        {
            state = std::make_unique<State>(std::move(cancelAction));
        }
    }

    OptionsSubscription::~OptionsSubscription() = default;

    OptionsSubscription::OptionsSubscription(OptionsSubscription&&) noexcept = default;

    OptionsSubscription& OptionsSubscription::operator=(
        OptionsSubscription&&) noexcept = default;

    OptionsSubscription::operator bool() const noexcept
    {
        return static_cast<bool>(state);
    }

    void OptionsSubscription::Reset() noexcept
    {
        state.reset();
    }
}
