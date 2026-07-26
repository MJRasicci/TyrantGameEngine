/**
 * @file OptionsMonitor.hpp
 * @brief Atomic, transactional live options implementation.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "TGE/Options/IOptionsMonitor.hpp"
#include "TGE/Options/IOptionsProvider.hpp"

namespace TGE
{
    template<OptionsType TOptions>
    class OptionsBuilder;

    /**
     * @class OptionsMonitor
     * @brief Central mutation authority and publisher for one options type.
     *
     * Reload and update construct a private candidate, validate it, and publish
     * one immutable snapshot. Readers never observe partial mutation.
     */
    template<OptionsType TOptions>
    class OptionsMonitor final
        : public IOptionsMonitor<TOptions>,
          public std::enable_shared_from_this<OptionsMonitor<TOptions>>
    {
    public:
        using typename IOptionsMonitor<TOptions>::ChangeCallback;
        using typename IOptionsMonitor<TOptions>::Snapshot;

        OptionsMonitor();
        explicit OptionsMonitor(TOptions defaultsValue);

        OptionsMonitor(const OptionsMonitor&) = delete;
        OptionsMonitor& operator=(const OptionsMonitor&) = delete;
        OptionsMonitor(OptionsMonitor&&) = delete;
        OptionsMonitor& operator=(OptionsMonitor&&) = delete;

        [[nodiscard]] Snapshot Current() const noexcept override;
        [[nodiscard]] OptionsSnapshot<TOptions>
        CurrentSnapshot() const noexcept override;
        [[nodiscard]] std::uint64_t Version() const noexcept override;
        [[nodiscard]] std::optional<OptionsError> LastError() const override;
        OptionsSubscription Observe(ChangeCallback callback) override;
        OptionsSubscription OnChange(ChangeCallback callback) override;

        /**
         * @brief Rebuild from defaults and every registered source.
         */
        OptionsResult<std::uint64_t> Reload();

        /**
         * @brief Replace the current runtime value after validation.
         *
         * A subsequent Reload intentionally reconstructs from configured sources.
         */
        OptionsResult<std::uint64_t> Set(TOptions value);

        /**
         * @brief Copy and mutate the current value transactionally.
         */
        template<class TUpdate>
            requires std::invocable<TUpdate&, TOptions&> &&
                (std::same_as<
                     std::invoke_result_t<TUpdate&, TOptions&>,
                     void> ||
                 std::same_as<
                     std::invoke_result_t<TUpdate&, TOptions&>,
                     OptionsResult<void>>)
        OptionsResult<std::uint64_t> Update(TUpdate&& update);

    private:
        using SourceStep =
            std::move_only_function<OptionsResult<void>(TOptions&)>;
        using Validator =
            std::move_only_function<OptionsResult<void>(const TOptions&)>;

        struct CallbackSlot
        {
            std::mutex mutex;
            std::condition_variable finished;
            std::shared_ptr<ChangeCallback> callback;
            std::optional<OptionsChange<TOptions>> pending;
            Snapshot lastValue;
            bool active { true };
            bool initializing { false };
            bool invoking { false };
            std::thread::id invokingThread;
        };

        struct PendingNotification
        {
            OptionsChange<TOptions> change;
            std::vector<std::shared_ptr<CallbackSlot>> callbacks;
        };

        struct CallbackState
        {
            std::mutex mutex;
            std::uint64_t nextId { 1 };
            std::unordered_map<
                std::uint64_t,
                std::shared_ptr<CallbackSlot>> callbacks;
            std::deque<PendingNotification> notifications;
            bool dispatching { false };
        };

        struct PublishedState
        {
            Snapshot value;
            std::uint64_t version {};
        };

        OptionsResult<std::uint64_t> AddProvider(
            std::shared_ptr<IOptionsProvider<TOptions>> provider);
        OptionsResult<std::uint64_t> AddConfigure(
            SourceStep configure,
            std::string source);
        OptionsResult<std::uint64_t> AddValidator(
            std::move_only_function<bool(const TOptions&)> predicate,
            std::string message);

        OptionsResult<TOptions> BuildCandidateLocked();
        OptionsResult<std::uint64_t> PublishLocked(
            TOptions candidate,
            std::unique_lock<std::mutex>& lock);
        OptionsResult<std::uint64_t> FailLocked(OptionsError error);
        OptionsSubscription RegisterCallback(
            ChangeCallback callback,
            bool deliverCurrent);
        static void InvokeCallback(
            const std::shared_ptr<CallbackSlot>& slot,
            const OptionsChange<TOptions>& change) noexcept;
        static void DispatchNotifications(
            std::shared_ptr<CallbackState> state) noexcept;

        TOptions defaults;
        std::atomic<std::shared_ptr<const PublishedState>> published;

        mutable std::mutex updateMutex;
        std::string currentSerialized;
        std::vector<SourceStep> sources;
        std::vector<Validator> validators;
        std::vector<OptionsSubscription> providerSubscriptions;
        std::optional<OptionsError> lastError;

        std::shared_ptr<CallbackState> callbackState;

        friend class OptionsBuilder<TOptions>;
    };
}

#include "TGE/Options/OptionsMonitor.inl"
