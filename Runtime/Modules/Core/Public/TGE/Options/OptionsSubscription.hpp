/**
 * @file OptionsSubscription.hpp
 * @brief Move-only lifetime token for options change subscriptions.
 */

#pragma once

#include <functional>
#include <memory>

#include "TGE/Export.hpp"

namespace TGE
{
    /**
     * @brief Runs a producer-defined cancellation action on reset or destruction.
     */
    class TGE_API OptionsSubscription final
    {
    public:
        OptionsSubscription() noexcept;
        explicit OptionsSubscription(std::function<void()> cancelAction);
        ~OptionsSubscription();

        OptionsSubscription(const OptionsSubscription&) = delete;
        OptionsSubscription& operator=(const OptionsSubscription&) = delete;

        OptionsSubscription(OptionsSubscription&&) noexcept;
        OptionsSubscription& operator=(OptionsSubscription&&) noexcept;

        /**
         * @brief Whether this token currently owns a cancellation action.
         */
        [[nodiscard]] explicit operator bool() const noexcept;

        /**
         * @brief Run the cancellation action now.
         *
         * The producer defines whether cancellation also fences work already
         * in flight. Repeated calls are harmless.
         */
        void Reset() noexcept;

    private:
        struct State;
        std::unique_ptr<State> state;
    };
}
