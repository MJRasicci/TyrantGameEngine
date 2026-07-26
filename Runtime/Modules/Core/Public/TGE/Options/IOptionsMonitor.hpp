/**
 * @file IOptionsMonitor.hpp
 * @brief Read-only live options contract injected into ordinary consumers.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "TGE/Options/OptionsError.hpp"
#include "TGE/Options/OptionsSerialization.hpp"
#include "TGE/Options/OptionsSubscription.hpp"

namespace TGE
{
    /**
     * @brief One coherent options value and its publication version.
     */
    template<OptionsType TOptions>
    struct OptionsSnapshot
    {
        std::shared_ptr<const TOptions> value;
        std::uint64_t version {};

        [[nodiscard]] const TOptions* operator->() const noexcept
        {
            return value.get();
        }

        [[nodiscard]] const TOptions& operator*() const noexcept
        {
            return *value;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return static_cast<bool>(value);
        }
    };

    /**
     * @brief One ordered options observation or atomically published change.
     *
     * Observe delivers an initial value with an empty previous snapshot.
     * OnChange delivers only later changes, whose previous snapshot is always
     * populated.
     */
    template<OptionsType TOptions>
    struct OptionsChange
    {
        std::shared_ptr<const TOptions> previous;
        std::shared_ptr<const TOptions> current;
        std::uint64_t version {};
    };

    /**
     * @class IOptionsMonitor
     * @brief Read-only singleton view of one live options type.
     *
     * Current returns an owning immutable snapshot. Consumers can safely retain
     * it across concurrent reloads without locks or dangling references.
     */
    template<OptionsType TOptions>
    class IOptionsMonitor
    {
    public:
        using Snapshot = std::shared_ptr<const TOptions>;
        using ChangeCallback =
            std::function<void(const OptionsChange<TOptions>&)>;

        virtual ~IOptionsMonitor() = default;

        /**
         * @brief Atomically acquire the current immutable snapshot.
         */
        [[nodiscard]] virtual Snapshot Current() const noexcept = 0;

        /**
         * @brief Atomically acquire the value and its matching version.
         */
        [[nodiscard]] virtual OptionsSnapshot<TOptions>
        CurrentSnapshot() const noexcept = 0;

        /**
         * @brief Monotonically increasing version of the current snapshot.
         *
         * Use CurrentSnapshot when the value and version must describe the
         * same publication.
         */
        [[nodiscard]] virtual std::uint64_t Version() const noexcept = 0;

        /**
         * @brief Most recent failed reload/update, cleared by a success.
         */
        [[nodiscard]] virtual std::optional<OptionsError> LastError() const = 0;

        /**
         * @brief Observe the current snapshot followed by later changes.
         *
         * The callback receives the current snapshot synchronously before this
         * function returns. Its first change has an empty previous pointer.
         * Concurrent publications are buffered and then delivered in version
         * order, so consumers should initialize and update cached state through
         * this one callback path.
         */
        virtual OptionsSubscription Observe(ChangeCallback callback) = 0;

        /**
         * @brief Observe future distinct snapshots.
         *
         * The callback runs after publication and outside monitor locks. Destroy
         * the returned token to unsubscribe and fence any callback already in
         * flight. Notifications are delivered in published version order.
         */
        virtual OptionsSubscription OnChange(ChangeCallback callback) = 0;
    };
}
