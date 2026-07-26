#include "TGE/Options/Providers/JsonFileOptionsProvider.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace TGE::detail
{
    namespace
    {
        constexpr std::uint64_t FileHashOffsetBasis =
            14'695'981'039'346'656'037ULL;
        constexpr std::uint64_t FileHashPrime = 1'099'511'628'211ULL;

        struct FileContentRevision
        {
            bool exists { false };
            std::uintmax_t size {};
            std::uint64_t contentHash {};

            bool operator==(const FileContentRevision&) const = default;
        };

        std::uint64_t HashContents(std::string_view contents) noexcept
        {
            std::uint64_t hash = FileHashOffsetBasis;
            for (const unsigned char value : contents)
            {
                hash ^= value;
                hash *= FileHashPrime;
            }
            return hash;
        }

        struct FileSignature
        {
            bool valid { false };
            bool exists { false };
            bool readable { false };
            std::filesystem::file_time_type writeTime {};
            std::uintmax_t size {};
            std::uint64_t contentHash {};

            bool operator==(const FileSignature&) const = default;
        };

        FileSignature ReadSignature(const std::filesystem::path& path)
        {
            std::error_code error;
            FileSignature signature;
            signature.exists = std::filesystem::exists(path, error);
            if (error)
            {
                return {};
            }

            signature.valid = true;
            if (!signature.exists)
            {
                return signature;
            }

            signature.writeTime =
                std::filesystem::last_write_time(path, error);
            if (error)
            {
                return { .exists = true };
            }

            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                return { .exists = true };
            }

            std::uint64_t hash = FileHashOffsetBasis;
            std::array<char, 4096> buffer {};
            while (stream)
            {
                stream.read(
                    buffer.data(),
                    static_cast<std::streamsize>(buffer.size()));
                const auto count = stream.gcount();
                signature.size += static_cast<std::uintmax_t>(count);
                for (std::streamsize index = 0; index < count; ++index)
                {
                    hash ^= static_cast<unsigned char>(
                        buffer[static_cast<std::size_t>(index)]);
                    hash *= FileHashPrime;
                }
            }

            if (stream.bad())
            {
                return { .exists = true };
            }

            signature.readable = true;
            signature.contentHash = hash;
            return signature;
        }

        bool Matches(
            const FileSignature& signature,
            const FileContentRevision& revision) noexcept
        {
            if (!signature.valid ||
                signature.exists != revision.exists)
            {
                return false;
            }

            if (!revision.exists)
            {
                return true;
            }

            return signature.readable &&
                signature.size == revision.size &&
                signature.contentHash == revision.contentHash;
        }
    }

    class OptionsFileWatchState final
    {
    public:
        struct ApplyObservation
        {
            ApplyObservation(
                std::thread::id ownerValue,
                bool captureFirstOnlyValue)
                : owner(ownerValue),
                  captureFirstOnly(captureFirstOnlyValue)
            {
            }

            std::thread::id owner;
            bool captureFirstOnly { false };
            std::optional<FileContentRevision> revision;
        };

        void Record(bool exists, std::string_view contents)
        {
            FileContentRevision revision {
                .exists = exists,
                .size = exists ? contents.size() : 0,
                .contentHash =
                    exists ? HashContents(contents) : 0
            };

            std::scoped_lock lock(mutex);

            const auto currentThread = std::this_thread::get_id();
            std::erase_if(
                observations,
                [&](const std::weak_ptr<ApplyObservation>& weak)
                {
                    auto observation = weak.lock();
                    if (!observation)
                    {
                        return true;
                    }

                    if (observation->owner == currentThread)
                    {
                        if (!observation->captureFirstOnly ||
                            !observation->revision)
                        {
                            observation->revision = revision;
                        }
                    }
                    return false;
                });
        }

        [[nodiscard]] std::shared_ptr<ApplyObservation>
        ObserveCurrentThread(bool captureFirstOnly = false)
        {
            auto observation = std::make_shared<ApplyObservation>(
                std::this_thread::get_id(),
                captureFirstOnly);
            std::scoped_lock lock(mutex);
            observations.emplace_back(observation);
            return observation;
        }

        [[nodiscard]] std::optional<FileContentRevision> Observed(
            const std::shared_ptr<ApplyObservation>& observation) const
        {
            std::scoped_lock lock(mutex);
            return observation->revision;
        }

        void Release(
            const std::shared_ptr<ApplyObservation>& observation)
        {
            std::scoped_lock lock(mutex);
            std::erase_if(
                observations,
                [&](const std::weak_ptr<ApplyObservation>& weak)
                {
                    auto current = weak.lock();
                    return !current ||
                        current.get() == observation.get();
                });
        }

    private:
        mutable std::mutex mutex;
        std::vector<std::weak_ptr<ApplyObservation>> observations;
    };

    namespace
    {
        class FileWatchState final
            : public std::enable_shared_from_this<FileWatchState>
        {
        public:
            FileWatchState(
                std::filesystem::path pathValue,
                std::chrono::milliseconds intervalValue,
                std::function<bool()> callbackValue,
                std::shared_ptr<OptionsFileWatchState> appliedStateValue)
                : path(std::move(pathValue)),
                  interval(std::max(
                      intervalValue,
                      std::chrono::milliseconds(10))),
                  callback(std::move(callbackValue)),
                  appliedState(std::move(appliedStateValue)),
                  initialObservation(
                      appliedState
                          ? appliedState->ObserveCurrentThread(true)
                          : nullptr)
            {
            }

            ~FileWatchState()
            {
                Stop();
                if (appliedState && initialObservation)
                {
                    appliedState->Release(initialObservation);
                }
            }

            void Start()
            {
                worker = std::jthread(
                    [self = shared_from_this()](std::stop_token stopping)
                    {
                        self->Run(stopping);
                    });
            }

            void Stop() noexcept
            {
                worker.request_stop();
                wake.notify_all();

                if (!worker.joinable())
                {
                    return;
                }

                if (worker.get_id() == std::this_thread::get_id())
                {
                    worker.detach();
                }
                else
                {
                    worker.join();
                }
            }

        private:
            void Run(std::stop_token stopping) noexcept
            {
                std::unique_lock lock(mutex);

                while (!stopping.stop_requested())
                {
                    wake.wait_for(
                        lock,
                        stopping,
                        interval,
                        [] { return false; });

                    if (stopping.stop_requested())
                    {
                        break;
                    }

                    lock.unlock();
                    FileSignature current;
                    try
                    {
                        current = ReadSignature(path);
                    }
                    catch (...)
                    {
                        lock.lock();
                        continue;
                    }

                    if (initialObservation)
                    {
                        auto initial =
                            appliedState->Observed(initialObservation);
                        if (!initial)
                        {
                            lock.lock();
                            continue;
                        }

                        acknowledgedRevision = std::move(*initial);
                        appliedState->Release(initialObservation);
                        initialObservation.reset();
                    }

                    if (acknowledgedRevision)
                    {
                        if (Matches(current, *acknowledgedRevision))
                        {
                            previous = current;
                            acknowledgedRevision.reset();
                            needsAcceptance = false;
                            lock.lock();
                            continue;
                        }
                    }
                    else if (!needsAcceptance &&
                             previous &&
                             current == *previous)
                    {
                        lock.lock();
                        continue;
                    }

                    std::shared_ptr<
                        OptionsFileWatchState::ApplyObservation>
                        observation;
                    if (appliedState)
                    {
                        observation =
                            appliedState->ObserveCurrentThread();
                    }

                    bool accepted = false;
                    try
                    {
                        accepted = callback();
                    }
                    catch (...)
                    {
                        // Provider notifications cannot safely propagate
                        // through the watcher thread.
                    }

                    std::optional<FileContentRevision> observed;
                    if (appliedState)
                    {
                        observed = appliedState->Observed(observation);
                        appliedState->Release(observation);
                    }

                    if (!accepted)
                    {
                        acknowledgedRevision.reset();
                        needsAcceptance = true;
                    }
                    else if (!appliedState)
                    {
                        if (current.valid)
                        {
                            previous = current;
                            needsAcceptance = false;
                        }
                        else
                        {
                            needsAcceptance = true;
                        }
                    }
                    else
                    {
                        if (!observed)
                        {
                            acknowledgedRevision.reset();
                            needsAcceptance = true;
                        }
                        else if (Matches(current, *observed))
                        {
                            previous = current;
                            acknowledgedRevision.reset();
                            needsAcceptance = false;
                        }
                        else
                        {
                            // Reload may have consumed a newer revision than
                            // the one polled. A later poll can acknowledge that
                            // exact successfully published content without
                            // reloading it a second time.
                            acknowledgedRevision = std::move(*observed);
                            needsAcceptance = false;
                        }
                    }
                    lock.lock();
                }
            }

            std::filesystem::path path;
            std::chrono::milliseconds interval;
            std::function<bool()> callback;
            std::shared_ptr<OptionsFileWatchState> appliedState;
            std::shared_ptr<OptionsFileWatchState::ApplyObservation>
                initialObservation;
            std::optional<FileSignature> previous;
            std::optional<FileContentRevision> acknowledgedRevision;
            bool needsAcceptance { false };

            std::mutex mutex;
            std::condition_variable_any wake;
            std::jthread worker;
        };
    }

    std::shared_ptr<OptionsFileWatchState>
    CreateOptionsFileWatchState()
    {
        return std::make_shared<OptionsFileWatchState>();
    }

    void RecordAppliedOptionsFile(
        const std::shared_ptr<OptionsFileWatchState>& state,
        bool exists,
        std::string_view contents)
    {
        if (state)
        {
            state->Record(exists, contents);
        }
    }

    OptionsSubscription WatchOptionsFile(
        std::filesystem::path path,
        std::chrono::milliseconds pollingInterval,
        std::function<bool()> callback,
        std::shared_ptr<OptionsFileWatchState> appliedState)
    {
        if (!callback)
        {
            return {};
        }

        auto state = std::make_shared<FileWatchState>(
            std::move(path),
            pollingInterval,
            std::move(callback),
            std::move(appliedState));
        state->Start();

        return OptionsSubscription(
            [state = std::move(state)] noexcept
            {
                state->Stop();
            });
    }
}
