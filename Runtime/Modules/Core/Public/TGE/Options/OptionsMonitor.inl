#pragma once

#include <exception>
#include <format>
#include <string_view>
#include <utility>

namespace TGE
{
    template<OptionsType TOptions>
    OptionsMonitor<TOptions>::OptionsMonitor()
        : OptionsMonitor(TOptions {})
    {
    }

    template<OptionsType TOptions>
    OptionsMonitor<TOptions>::OptionsMonitor(TOptions defaultsValue)
        : defaults(std::move(defaultsValue)),
          callbackState(std::make_shared<CallbackState>())
    {
        auto snapshot = std::make_shared<const TOptions>(this->defaults);
        auto serialized = SerializeOptions(*snapshot);
        if (!serialized)
        {
            throw OptionsException(std::move(serialized.error()));
        }

        currentSerialized = std::move(*serialized);
        published.store(
            std::make_shared<const PublishedState>(PublishedState {
                .value = std::move(snapshot),
                .version = 0
            }),
            std::memory_order_release);
    }

    template<OptionsType TOptions>
    typename OptionsMonitor<TOptions>::Snapshot
    OptionsMonitor<TOptions>::Current() const noexcept
    {
        return published.load(std::memory_order_acquire)->value;
    }

    template<OptionsType TOptions>
    OptionsSnapshot<TOptions>
    OptionsMonitor<TOptions>::CurrentSnapshot() const noexcept
    {
        const auto state = published.load(std::memory_order_acquire);
        return OptionsSnapshot<TOptions> {
            .value = state->value,
            .version = state->version
        };
    }

    template<OptionsType TOptions>
    std::uint64_t OptionsMonitor<TOptions>::Version() const noexcept
    {
        return published.load(std::memory_order_acquire)->version;
    }

    template<OptionsType TOptions>
    std::optional<OptionsError> OptionsMonitor<TOptions>::LastError() const
    {
        std::scoped_lock lock(updateMutex);
        return lastError;
    }

    template<OptionsType TOptions>
    OptionsSubscription OptionsMonitor<TOptions>::Observe(
        ChangeCallback callback)
    {
        return RegisterCallback(std::move(callback), true);
    }

    template<OptionsType TOptions>
    OptionsSubscription OptionsMonitor<TOptions>::OnChange(
        ChangeCallback callback)
    {
        return RegisterCallback(std::move(callback), false);
    }

    template<OptionsType TOptions>
    OptionsSubscription OptionsMonitor<TOptions>::RegisterCallback(
        ChangeCallback callback,
        bool deliverCurrent)
    {
        if (!callback)
        {
            return {};
        }

        [[maybe_unused]] auto keepAlive = this->weak_from_this().lock();
        auto state = callbackState;
        auto slot = std::make_shared<CallbackSlot>();
        slot->callback =
            std::make_shared<ChangeCallback>(std::move(callback));
        slot->initializing = deliverCurrent;

        std::uint64_t id {};
        {
            std::scoped_lock lock(state->mutex);
            id = state->nextId++;
        }

        std::weak_ptr<CallbackState> weakState = state;
        auto subscription = OptionsSubscription(
            [weakState, slot, id] noexcept
            {
                if (auto currentState = weakState.lock())
                {
                    std::scoped_lock lock(currentState->mutex);
                    currentState->callbacks.erase(id);
                }

                std::shared_ptr<ChangeCallback> retiredCallback;
                std::optional<OptionsChange<TOptions>> retiredPending;
                {
                    std::unique_lock lock(slot->mutex);
                    slot->active = false;
                    slot->initializing = false;
                    retiredPending = std::move(slot->pending);

                    if (slot->invoking &&
                        slot->invokingThread != std::this_thread::get_id())
                    {
                        slot->finished.wait(
                            lock,
                            [&slot] { return !slot->invoking; });
                    }

                    if (!slot->invoking)
                    {
                        retiredCallback = std::move(slot->callback);
                    }
                }
            });

        std::shared_ptr<const PublishedState> currentState;
        {
            std::scoped_lock lock(state->mutex);
            currentState = published.load(std::memory_order_acquire);
            slot->lastValue = currentState->value;
            state->callbacks.emplace(id, slot);
        }

        if (!deliverCurrent)
        {
            return subscription;
        }

        InvokeCallback(
            slot,
            OptionsChange<TOptions> {
                .previous = {},
                .current = currentState->value,
                .version = currentState->version
            });

        while (true)
        {
            std::optional<OptionsChange<TOptions>> pending;
            {
                std::scoped_lock lock(slot->mutex);
                if (!slot->active)
                {
                    slot->initializing = false;
                    break;
                }

                if (!slot->pending)
                {
                    slot->initializing = false;
                    break;
                }

                pending = std::move(slot->pending);
                slot->pending.reset();
            }

            InvokeCallback(slot, *pending);
        }

        return subscription;
    }

    template<OptionsType TOptions>
    void OptionsMonitor<TOptions>::InvokeCallback(
        const std::shared_ptr<CallbackSlot>& slot,
        const OptionsChange<TOptions>& change) noexcept
    {
        std::shared_ptr<ChangeCallback> callback;
        auto delivered = change;
        try
        {
            std::unique_lock lock(slot->mutex);
            if (!slot->active)
            {
                return;
            }

            if (delivered.previous)
            {
                delivered.previous = slot->lastValue;
            }
            callback = slot->callback;
            slot->invoking = true;
            slot->invokingThread = std::this_thread::get_id();
        }
        catch (...)
        {
            return;
        }

        try
        {
            std::invoke(*callback, delivered);
        }
        catch (...)
        {
            // One consumer cannot suppress later observers or publications.
        }

        std::shared_ptr<ChangeCallback> retiredCallback;
        {
            std::scoped_lock lock(slot->mutex);
            slot->lastValue = delivered.current;
            slot->invoking = false;
            slot->invokingThread = {};
            if (!slot->active)
            {
                retiredCallback = std::move(slot->callback);
            }
        }
        slot->finished.notify_all();
    }

    template<OptionsType TOptions>
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::Reload()
    {
        [[maybe_unused]] auto keepAlive = this->weak_from_this().lock();
        std::unique_lock lock(updateMutex);
        auto candidate = BuildCandidateLocked();
        if (!candidate)
        {
            return FailLocked(std::move(candidate.error()));
        }

        return PublishLocked(std::move(*candidate), lock);
    }

    template<OptionsType TOptions>
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::Set(
        TOptions value)
    {
        [[maybe_unused]] auto keepAlive = this->weak_from_this().lock();
        std::unique_lock lock(updateMutex);
        return PublishLocked(std::move(value), lock);
    }

    template<OptionsType TOptions>
    template<class TUpdate>
        requires std::invocable<TUpdate&, TOptions&> &&
            (std::same_as<
                 std::invoke_result_t<TUpdate&, TOptions&>,
                 void> ||
             std::same_as<
                 std::invoke_result_t<TUpdate&, TOptions&>,
                 OptionsResult<void>>)
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::Update(
        TUpdate&& update)
    {
        [[maybe_unused]] auto keepAlive = this->weak_from_this().lock();
        std::unique_lock lock(updateMutex);
        TOptions candidate =
            *published.load(std::memory_order_acquire)->value;

        try
        {
            if constexpr (std::same_as<
                              std::invoke_result_t<TUpdate&, TOptions&>,
                              void>)
            {
                std::invoke(update, candidate);
            }
            else
            {
                auto result = std::invoke(update, candidate);
                if (!result)
                {
                    auto error = std::move(result.error());
                    if (error.source.empty())
                    {
                        error.source = "runtime update";
                    }
                    return FailLocked(std::move(error));
                }
            }
        }
        catch (const std::exception& exception)
        {
            return FailLocked(OptionsError {
                .code = OptionsErrorCode::Update,
                .source = "runtime update",
                .message = exception.what()
            });
        }
        catch (...)
        {
            return FailLocked(OptionsError {
                .code = OptionsErrorCode::Update,
                .source = "runtime update",
                .message = "The update callback threw an unknown exception."
            });
        }

        return PublishLocked(std::move(candidate), lock);
    }

    template<OptionsType TOptions>
    OptionsResult<TOptions>
    OptionsMonitor<TOptions>::BuildCandidateLocked()
    {
        TOptions candidate = defaults;
        for (auto& source : sources)
        {
            auto result = source(candidate);
            if (!result)
            {
                return std::unexpected(std::move(result.error()));
            }
        }

        return candidate;
    }

    template<OptionsType TOptions>
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::AddProvider(
        std::shared_ptr<IOptionsProvider<TOptions>> provider)
    {
        if (!provider)
        {
            std::scoped_lock lock(updateMutex);
            return FailLocked(OptionsError {
                .code = OptionsErrorCode::Provider,
                .source = "options registration",
                .message = "Cannot register a null options provider."
            });
        }

        const std::string sourceName(provider->Name());
        SourceStep providerStep =
            [provider, sourceName](
                TOptions& candidate) -> OptionsResult<void>
            {
                try
                {
                    auto result = provider->Apply(candidate);
                    if (!result && result.error().source.empty())
                    {
                        result.error().source = sourceName;
                    }
                    return result;
                }
                catch (const std::exception& exception)
                {
                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Provider,
                        .source = sourceName,
                        .message = exception.what()
                    });
                }
                catch (...)
                {
                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Provider,
                        .source = sourceName,
                        .message =
                            "The provider threw an unknown exception."
                    });
                }
            };

        struct WatchGate
        {
            std::weak_ptr<OptionsMonitor<TOptions>> monitor;
            std::atomic<bool> active { false };
            std::atomic<bool> pending { false };

            bool Notify() noexcept
            {
                pending.store(true, std::memory_order_release);
                if (active.load(std::memory_order_acquire))
                {
                    return ReloadPending();
                }
                return false;
            }

            void Activate() noexcept
            {
                active.store(true, std::memory_order_release);
                ReloadPending();
            }

        private:
            bool ReloadPending() noexcept
            {
                if (!pending.exchange(false, std::memory_order_acq_rel))
                {
                    return true;
                }

                if (auto current = monitor.lock())
                {
                    return static_cast<bool>(current->Reload());
                }
                return false;
            }
        };

        auto gate = std::make_shared<WatchGate>();
        gate->monitor = this->weak_from_this();

        OptionsSubscription subscription;
        try
        {
            subscription = provider->Watch(
                [gate] { return gate->Notify(); });
        }
        catch (const std::exception& exception)
        {
            std::scoped_lock lock(updateMutex);
            return FailLocked(OptionsError {
                .code = OptionsErrorCode::Provider,
                .source = sourceName,
                .message = std::format(
                    "Failed to monitor provider changes: {}",
                    exception.what())
            });
        }
        catch (...)
        {
            std::scoped_lock lock(updateMutex);
            return FailLocked(OptionsError {
                .code = OptionsErrorCode::Provider,
                .source = sourceName,
                .message = "Failed to monitor provider changes."
            });
        }

        std::unique_lock lock(updateMutex);
        auto candidate = BuildCandidateLocked();
        if (!candidate)
        {
            auto result = FailLocked(std::move(candidate.error()));
            lock.unlock();
            subscription.Reset();
            return result;
        }

        auto providerResult = providerStep(*candidate);
        if (!providerResult)
        {
            auto result = FailLocked(std::move(providerResult.error()));
            lock.unlock();
            subscription.Reset();
            return result;
        }

        sources.emplace_back(std::move(providerStep));
        try
        {
            // Keep one token per provider source, including providers that do
            // not actively monitor, so rollback never relies on a side table.
            providerSubscriptions.emplace_back(std::move(subscription));
        }
        catch (...)
        {
            sources.pop_back();
            throw;
        }

        OptionsResult<std::uint64_t> result;
        try
        {
            result = PublishLocked(std::move(*candidate), lock);
        }
        catch (...)
        {
            if (lock.owns_lock())
            {
                auto rejected =
                    std::move(providerSubscriptions.back());
                providerSubscriptions.pop_back();
                sources.pop_back();
                lock.unlock();
                rejected.Reset();
            }
            throw;
        }

        if (!result)
        {
            auto rejected = std::move(providerSubscriptions.back());
            providerSubscriptions.pop_back();
            sources.pop_back();
            lock.unlock();
            rejected.Reset();
            return result;
        }

        if (lock.owns_lock())
        {
            lock.unlock();
        }
        gate->Activate();
        return result;
    }

    template<OptionsType TOptions>
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::AddConfigure(
        SourceStep configure,
        std::string source)
    {
        if (!configure)
        {
            std::scoped_lock lock(updateMutex);
            return FailLocked(OptionsError {
                .code = OptionsErrorCode::Provider,
                .source = std::move(source),
                .message = "Cannot register an empty configure callback."
            });
        }

        SourceStep configureStep =
            [configure = std::move(configure), source](
                TOptions& candidate) mutable -> OptionsResult<void>
            {
                try
                {
                    auto result = configure(candidate);
                    if (!result && result.error().source.empty())
                    {
                        result.error().source = source;
                    }
                    return result;
                }
                catch (const std::exception& exception)
                {
                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Provider,
                        .source = source,
                        .message = exception.what()
                    });
                }
                catch (...)
                {
                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Provider,
                        .source = source,
                        .message =
                            "The configure callback threw an unknown exception."
                    });
                }
            };

        std::unique_lock lock(updateMutex);
        auto candidate = BuildCandidateLocked();
        if (!candidate)
        {
            return FailLocked(std::move(candidate.error()));
        }

        auto configureResult = configureStep(*candidate);
        if (!configureResult)
        {
            return FailLocked(std::move(configureResult.error()));
        }

        sources.emplace_back(std::move(configureStep));
        OptionsResult<std::uint64_t> result;
        try
        {
            result = PublishLocked(std::move(*candidate), lock);
        }
        catch (...)
        {
            if (lock.owns_lock())
            {
                sources.pop_back();
            }
            throw;
        }

        if (!result)
        {
            sources.pop_back();
        }
        return result;
    }

    template<OptionsType TOptions>
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::AddValidator(
        std::move_only_function<bool(const TOptions&)> predicate,
        std::string message)
    {
        if (!predicate)
        {
            std::scoped_lock lock(updateMutex);
            return FailLocked(OptionsError {
                .code = OptionsErrorCode::Validation,
                .source = "options validation",
                .message = "Cannot register an empty validation predicate."
            });
        }

        Validator validator =
            [predicate = std::move(predicate), message](
                const TOptions& candidate) mutable -> OptionsResult<void>
            {
                try
                {
                    if (predicate(candidate))
                    {
                        return {};
                    }

                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Validation,
                        .source = "options validation",
                        .message = message
                    });
                }
                catch (const std::exception& exception)
                {
                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Validation,
                        .source = "options validation",
                        .message = std::format(
                            "{} ({})",
                            message,
                            exception.what())
                    });
                }
                catch (...)
                {
                    return std::unexpected(OptionsError {
                        .code = OptionsErrorCode::Validation,
                        .source = "options validation",
                        .message = std::format(
                            "{} (predicate threw an unknown exception)",
                            message)
                    });
                }
            };

        std::unique_lock lock(updateMutex);
        auto candidate = BuildCandidateLocked();
        if (!candidate)
        {
            return FailLocked(std::move(candidate.error()));
        }

        validators.emplace_back(std::move(validator));
        OptionsResult<std::uint64_t> result;
        try
        {
            result = PublishLocked(std::move(*candidate), lock);
        }
        catch (...)
        {
            if (lock.owns_lock())
            {
                validators.pop_back();
            }
            throw;
        }

        if (!result)
        {
            validators.pop_back();
        }
        return result;
    }

    template<OptionsType TOptions>
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::PublishLocked(
        TOptions candidate,
        std::unique_lock<std::mutex>& lock)
    {
        for (auto& validator : validators)
        {
            auto result = validator(candidate);
            if (!result)
            {
                return FailLocked(std::move(result.error()));
            }
        }

        auto serialized = SerializeOptions(candidate);
        if (!serialized)
        {
            return FailLocked(std::move(serialized.error()));
        }

        if (*serialized == currentSerialized)
        {
            lastError.reset();
            return published.load(std::memory_order_acquire)->version;
        }

        const auto previousState =
            published.load(std::memory_order_acquire);
        auto previous = previousState->value;
        auto next = std::make_shared<const TOptions>(std::move(candidate));
        const auto nextVersion = previousState->version + 1;
        auto nextState =
            std::make_shared<const PublishedState>(PublishedState {
                .value = next,
                .version = nextVersion
            });

        std::vector<std::shared_ptr<CallbackSlot>> callbacks;
        bool dispatchNotifications = false;
        {
            std::scoped_lock callbackLock(callbackState->mutex);
            callbacks.reserve(callbackState->callbacks.size());
            for (const auto& [id, slot] : callbackState->callbacks)
            {
                static_cast<void>(id);
                callbacks.emplace_back(slot);
            }

            auto notification = PendingNotification {
                .change = {
                    .previous = previous,
                    .current = next,
                    .version = nextVersion
                },
                .callbacks = std::move(callbacks)
            };

            if (callbackState->notifications.empty())
            {
                callbackState->notifications.emplace_back(
                    std::move(notification));
            }
            else
            {
                // Options are state, not an event log. While one notification
                // is in flight, retain only the newest pending state and the
                // earliest skipped predecessor.
                notification.change.previous =
                    callbackState->notifications.back().change.previous;
                callbackState->notifications.back() =
                    std::move(notification);
            }

            if (!callbackState->dispatching)
            {
                callbackState->dispatching = true;
                dispatchNotifications = true;
            }

            currentSerialized = std::move(*serialized);
            lastError.reset();
            published.store(std::move(nextState), std::memory_order_release);
        }

        lock.unlock();

        if (dispatchNotifications)
        {
            DispatchNotifications(callbackState);
        }

        return nextVersion;
    }

    template<OptionsType TOptions>
    OptionsResult<std::uint64_t> OptionsMonitor<TOptions>::FailLocked(
        OptionsError error)
    {
        lastError = error;
        return std::unexpected(std::move(error));
    }

    template<OptionsType TOptions>
    void OptionsMonitor<TOptions>::DispatchNotifications(
        std::shared_ptr<CallbackState> state) noexcept
    {
        while (true)
        {
            std::optional<PendingNotification> notification;
            {
                std::scoped_lock lock(state->mutex);
                if (state->notifications.empty())
                {
                    state->dispatching = false;
                    return;
                }

                notification.emplace(
                    std::move(state->notifications.front()));
                state->notifications.pop_front();
            }

            for (const auto& slot : notification->callbacks)
            {
                try
                {
                    std::unique_lock lock(slot->mutex);
                    if (!slot->active)
                    {
                        continue;
                    }

                    if (slot->initializing)
                    {
                        if (slot->pending)
                        {
                            slot->pending->current =
                                notification->change.current;
                            slot->pending->version =
                                notification->change.version;
                        }
                        else
                        {
                            slot->pending = notification->change;
                        }
                        continue;
                    }
                }
                catch (...)
                {
                    // A callback that cannot be buffered safely is skipped.
                    continue;
                }

                InvokeCallback(slot, notification->change);
            }
        }
    }
}
