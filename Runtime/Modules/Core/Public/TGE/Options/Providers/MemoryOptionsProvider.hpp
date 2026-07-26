/**
 * @file MemoryOptionsProvider.hpp
 * @brief Observable in-memory override source for typed options.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "TGE/Options/IOptionsProvider.hpp"

namespace TGE
{
    /**
     * @class MemoryOptionsProvider
     * @brief Last-writer-friendly runtime source useful for tools and tests.
     *
     * Set requests a complete override from every monitor watching this
     * provider. Clear requests reconstruction from earlier sources. Either
     * operation reports false when any monitor rejects the resulting value.
     */
    template<OptionsType TOptions>
    class MemoryOptionsProvider final : public IOptionsProvider<TOptions>
    {
    public:
        explicit MemoryOptionsProvider(
            std::string nameValue = "in-memory options")
            : name(std::move(nameValue)),
              callbacks(std::make_shared<CallbackState>())
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return name;
        }

        OptionsResult<void> Apply(TOptions& candidate) const override
        {
            std::scoped_lock lock(valueMutex);
            if (value)
            {
                candidate = *value;
            }
            return {};
        }

        OptionsSubscription Watch(
            typename IOptionsProvider<TOptions>::ChangeCallback callback) override
        {
            if (!callback)
            {
                return {};
            }

            std::uint64_t id {};
            {
                std::scoped_lock lock(callbacks->mutex);
                id = callbacks->nextId++;
            }

            std::weak_ptr<CallbackState> weakCallbacks = callbacks;
            auto subscription = OptionsSubscription(
                [weakCallbacks, id] noexcept
                {
                    if (auto state = weakCallbacks.lock())
                    {
                        typename IOptionsProvider<
                            TOptions>::ChangeCallback retired;
                        {
                            std::scoped_lock lock(state->mutex);
                            const auto entry = state->values.find(id);
                            if (entry != state->values.end())
                            {
                                retired = std::move(entry->second);
                                state->values.erase(entry);
                            }
                        }
                    }
                });

            {
                std::scoped_lock lock(callbacks->mutex);
                callbacks->values.emplace(id, std::move(callback));
            }

            return subscription;
        }

        /**
         * @brief Request publication of a complete in-memory override.
         * @return True when every attached monitor accepted the reload.
         */
        [[nodiscard]] bool Set(TOptions next)
        {
            {
                std::scoped_lock lock(valueMutex);
                value = std::move(next);
            }
            return Notify();
        }

        /**
         * @brief Request removal of the override.
         * @return True when every attached monitor accepted the reload.
         */
        [[nodiscard]] bool Clear()
        {
            {
                std::scoped_lock lock(valueMutex);
                value.reset();
            }
            return Notify();
        }

    private:
        struct CallbackState
        {
            std::mutex mutex;
            std::uint64_t nextId { 1 };
            std::unordered_map<
                std::uint64_t,
                typename IOptionsProvider<TOptions>::ChangeCallback> values;
        };

        bool Notify()
        {
            std::vector<typename IOptionsProvider<TOptions>::ChangeCallback>
                snapshot;
            {
                std::scoped_lock lock(callbacks->mutex);
                snapshot.reserve(callbacks->values.size());
                for (const auto& [id, callback] : callbacks->values)
                {
                    static_cast<void>(id);
                    snapshot.emplace_back(callback);
                }
            }

            bool accepted = true;
            for (const auto& callback : snapshot)
            {
                try
                {
                    const bool currentAccepted = callback();
                    accepted = currentAccepted && accepted;
                }
                catch (...)
                {
                    // One observer cannot suppress other reload notifications.
                    accepted = false;
                }
            }
            return accepted;
        }

        std::string name;
        mutable std::mutex valueMutex;
        std::optional<TOptions> value;
        std::shared_ptr<CallbackState> callbacks;
    };
}
