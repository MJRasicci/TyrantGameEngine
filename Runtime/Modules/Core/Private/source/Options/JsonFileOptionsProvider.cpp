#include "TGE/Options/Providers/JsonFileOptionsProvider.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

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

        OptionsResult<void> FileIoFailure(
            std::string_view source,
            std::string_view operation,
            const std::error_code& error)
        {
            return std::unexpected(OptionsError {
                .code = OptionsErrorCode::Io,
                .source = std::string(source),
                .message = std::format(
                    "{}: {}",
                    operation,
                    error.message())
            });
        }

        OptionsResult<void> FileIoFailure(
            std::string_view source,
            std::string message)
        {
            return std::unexpected(OptionsError {
                .code = OptionsErrorCode::Io,
                .source = std::string(source),
                .message = std::move(message)
            });
        }

        class TemporaryPathGuard final
        {
        public:
            explicit TemporaryPathGuard(std::filesystem::path pathValue)
                : path(std::move(pathValue))
            {
            }

            ~TemporaryPathGuard()
            {
                if (!path.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                }
            }

            TemporaryPathGuard(const TemporaryPathGuard&) = delete;
            TemporaryPathGuard& operator=(const TemporaryPathGuard&) = delete;

            void Release() noexcept
            {
                path.clear();
            }

        private:
            std::filesystem::path path;
        };

#if defined(_WIN32)
        OptionsResult<void> WriteOptionsFileNative(
            const std::filesystem::path& target,
            std::string_view contents,
            std::string_view source)
        {
            static std::atomic<std::uint64_t> nextTemporaryId {};

            HANDLE file = INVALID_HANDLE_VALUE;
            std::filesystem::path temporary;
            std::filesystem::path backup;
            DWORD creationError = ERROR_FILE_EXISTS;
            for (std::uint32_t attempt = 0; attempt < 256; ++attempt)
            {
                const auto id =
                    nextTemporaryId.fetch_add(1, std::memory_order_relaxed);
                auto targetStem = target.filename().native();
                constexpr std::size_t MaximumTemporaryStemLength = 48;
                if (targetStem.size() > MaximumTemporaryStemLength)
                {
                    targetStem.resize(MaximumTemporaryStemLength);
                }

                const auto uniqueSuffix =
                    std::to_wstring(GetCurrentProcessId()) +
                    L"." +
                    std::to_wstring(GetTickCount64()) +
                    L"." +
                    std::to_wstring(id);
                const auto temporaryName =
                    targetStem +
                    L".tmp." +
                    uniqueSuffix;
                temporary = target.parent_path() / temporaryName;
                backup = target.parent_path() /
                    (targetStem + L".backup." + uniqueSuffix);

                const DWORD backupAttributes =
                    GetFileAttributesW(backup.c_str());
                if (backupAttributes != INVALID_FILE_ATTRIBUTES)
                {
                    continue;
                }

                const auto backupInspectionError = GetLastError();
                if (backupInspectionError != ERROR_FILE_NOT_FOUND &&
                    backupInspectionError != ERROR_PATH_NOT_FOUND)
                {
                    return FileIoFailure(
                        source,
                        "A recovery backup path could not be inspected",
                        std::error_code(
                            static_cast<int>(backupInspectionError),
                            std::system_category()));
                }

                file = CreateFileW(
                    temporary.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
                if (file != INVALID_HANDLE_VALUE)
                {
                    break;
                }

                creationError = GetLastError();
                if (creationError != ERROR_FILE_EXISTS &&
                    creationError != ERROR_ALREADY_EXISTS)
                {
                    return FileIoFailure(
                        source,
                        "The temporary options file could not be created",
                        std::error_code(
                            static_cast<int>(creationError),
                            std::system_category()));
                }
            }

            if (file == INVALID_HANDLE_VALUE)
            {
                return FileIoFailure(
                    source,
                    "A unique temporary options file could not be created.");
            }

            TemporaryPathGuard cleanup(temporary);

            std::size_t offset = 0;
            while (offset < contents.size())
            {
                const auto remaining = contents.size() - offset;
                const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                    remaining,
                    std::numeric_limits<DWORD>::max()));
                DWORD written = 0;
                const BOOL writeSucceeded = WriteFile(
                    file,
                    contents.data() + offset,
                    chunk,
                    &written,
                    nullptr);
                if (!writeSucceeded ||
                    written == 0)
                {
                    const auto writeError = writeSucceeded
                        ? ERROR_WRITE_FAULT
                        : GetLastError();
                    CloseHandle(file);
                    file = INVALID_HANDLE_VALUE;
                    return FileIoFailure(
                        source,
                        "The temporary options file could not be written",
                        std::error_code(
                            static_cast<int>(writeError),
                            std::system_category()));
                }
                offset += written;
            }

            if (!FlushFileBuffers(file))
            {
                const auto flushError = GetLastError();
                CloseHandle(file);
                file = INVALID_HANDLE_VALUE;
                return FileIoFailure(
                    source,
                    "The temporary options file could not be flushed",
                    std::error_code(
                        static_cast<int>(flushError),
                        std::system_category()));
            }

            if (!CloseHandle(file))
            {
                const auto closeError = GetLastError();
                file = INVALID_HANDLE_VALUE;
                return FileIoFailure(
                    source,
                    "The temporary options file could not be closed",
                    std::error_code(
                        static_cast<int>(closeError),
                        std::system_category()));
            }
            file = INVALID_HANDLE_VALUE;

            const DWORD attributes = GetFileAttributesW(target.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES)
            {
                if (ReplaceFileW(
                        target.c_str(),
                        temporary.c_str(),
                        backup.c_str(),
                        0,
                        nullptr,
                        nullptr))
                {
                    if (!DeleteFileW(backup.c_str()))
                    {
                        const auto cleanupError = GetLastError();
                        cleanup.Release();
                        return FileIoFailure(
                            source,
                            "The options file was saved, but its temporary "
                            "backup could not be removed",
                            std::error_code(
                                static_cast<int>(cleanupError),
                                std::system_category()));
                    }

                    cleanup.Release();
                    return {};
                }

                const auto replaceError = GetLastError();
                if (replaceError ==
                    ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
                {
                    if (MoveFileExW(
                            backup.c_str(),
                            target.c_str(),
                            MOVEFILE_WRITE_THROUGH))
                    {
                        return FileIoFailure(
                            source,
                            "The options file replacement failed; the "
                            "previous document was restored",
                            std::error_code(
                                static_cast<int>(replaceError),
                                std::system_category()));
                    }

                    cleanup.Release();
                    return FileIoFailure(
                        source,
                        std::format(
                            "The options file replacement and recovery both "
                            "failed. Recovery files were retained at {} and "
                            "{}.",
                            temporary.string(),
                            backup.string()));
                }

                if (replaceError != ERROR_FILE_NOT_FOUND &&
                    replaceError != ERROR_PATH_NOT_FOUND)
                {
                    return FileIoFailure(
                        source,
                        "The options file could not be atomically replaced",
                        std::error_code(
                            static_cast<int>(replaceError),
                            std::system_category()));
                }
            }
            else
            {
                const auto attributesError = GetLastError();
                if (attributesError != ERROR_FILE_NOT_FOUND &&
                    attributesError != ERROR_PATH_NOT_FOUND)
                {
                    return FileIoFailure(
                        source,
                        "The options file destination could not be inspected",
                        std::error_code(
                            static_cast<int>(attributesError),
                            std::system_category()));
                }
            }

            if (!MoveFileExW(
                    temporary.c_str(),
                    target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                const auto moveError = GetLastError();
                return FileIoFailure(
                    source,
                    "The options file could not be atomically replaced",
                    std::error_code(
                        static_cast<int>(moveError),
                        std::system_category()));
            }

            cleanup.Release();
            return {};
        }
#else
        OptionsResult<void> WriteOptionsFileNative(
            const std::filesystem::path& target,
            std::string_view contents,
            std::string_view source)
        {
            std::optional<mode_t> destinationMode;
            struct stat destinationStatus {};
            if (::lstat(target.c_str(), &destinationStatus) == 0)
            {
                if (S_ISREG(destinationStatus.st_mode))
                {
                    constexpr mode_t PermissionBits =
                        S_IRWXU | S_IRWXG | S_IRWXO;
                    destinationMode =
                        destinationStatus.st_mode & PermissionBits;
                }
            }
            else if (errno != ENOENT)
            {
                return FileIoFailure(
                    source,
                    "The options file destination could not be inspected",
                    std::error_code(errno, std::generic_category()));
            }

            auto targetStem = target.filename().native();
            constexpr std::size_t MaximumTemporaryStemLength = 48;
            if (targetStem.size() > MaximumTemporaryStemLength)
            {
                targetStem.resize(MaximumTemporaryStemLength);
            }
            auto temporaryPattern =
                target.parent_path() /
                (targetStem + ".tmp.XXXXXX");
            auto nativePattern = temporaryPattern.native();
            std::vector<char> mutablePattern(
                nativePattern.begin(),
                nativePattern.end());
            mutablePattern.push_back('\0');

            int descriptor = ::mkstemp(mutablePattern.data());
            if (descriptor < 0)
            {
                return FileIoFailure(
                    source,
                    "The temporary options file could not be created",
                    std::error_code(errno, std::generic_category()));
            }

            TemporaryPathGuard cleanup(
                std::filesystem::path(mutablePattern.data()));
            const auto closeIgnoringErrors = [&descriptor]() noexcept
            {
                if (descriptor >= 0)
                {
                    const int current = descriptor;
                    descriptor = -1;
                    static_cast<void>(::close(current));
                }
            };

            if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0)
            {
                const auto error =
                    std::error_code(errno, std::generic_category());
                closeIgnoringErrors();
                return FileIoFailure(
                    source,
                    "The temporary options file could not be secured",
                    error);
            }

            std::size_t offset = 0;
            while (offset < contents.size())
            {
                const auto remaining = contents.size() - offset;
                const auto chunk = std::min<std::size_t>(
                    remaining,
                    static_cast<std::size_t>(
                        std::numeric_limits<ssize_t>::max()));

                ssize_t written = 0;
                do
                {
                    written = ::write(
                        descriptor,
                        contents.data() + offset,
                        chunk);
                }
                while (written < 0 && errno == EINTR);

                if (written <= 0)
                {
                    const auto error = written < 0
                        ? std::error_code(errno, std::generic_category())
                        : std::make_error_code(std::errc::io_error);
                    closeIgnoringErrors();
                    return FileIoFailure(
                        source,
                        "The temporary options file could not be written",
                        error);
                }
                offset += static_cast<std::size_t>(written);
            }

            if (destinationMode &&
                ::fchmod(descriptor, *destinationMode) < 0)
            {
                const auto error =
                    std::error_code(errno, std::generic_category());
                closeIgnoringErrors();
                return FileIoFailure(
                    source,
                    "The options file permissions could not be preserved",
                    error);
            }

            int flushResult = 0;
            do
            {
                flushResult = ::fsync(descriptor);
            }
            while (flushResult < 0 && errno == EINTR);

            if (flushResult < 0)
            {
                const auto error =
                    std::error_code(errno, std::generic_category());
                closeIgnoringErrors();
                return FileIoFailure(
                    source,
                    "The temporary options file could not be flushed",
                    error);
            }

            const int current = descriptor;
            descriptor = -1;
            if (::close(current) < 0)
            {
                return FileIoFailure(
                    source,
                    "The temporary options file could not be closed",
                    std::error_code(errno, std::generic_category()));
            }

            if (::rename(mutablePattern.data(), target.c_str()) < 0)
            {
                return FileIoFailure(
                    source,
                    "The options file could not be atomically replaced",
                    std::error_code(errno, std::generic_category()));
            }

            cleanup.Release();
            return {};
        }
#endif
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
                std::shared_ptr<OptionsFileWatchState> appliedStateValue,
                std::shared_ptr<std::mutex> fileMutexValue)
                : path(std::move(pathValue)),
                  interval(std::max(
                      intervalValue,
                      std::chrono::milliseconds(10))),
                  callback(std::move(callbackValue)),
                  appliedState(std::move(appliedStateValue)),
                  fileMutex(std::move(fileMutexValue)),
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
                        if (fileMutex)
                        {
                            std::scoped_lock fileLock(*fileMutex);
                            current = ReadSignature(path);
                        }
                        else
                        {
                            current = ReadSignature(path);
                        }
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
            std::shared_ptr<std::mutex> fileMutex;
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

    OptionsResult<void> WriteOptionsFileAtomically(
        const std::filesystem::path& path,
        std::string_view contents,
        std::string_view source)
    {
        std::error_code pathError;
        auto target = path.is_absolute()
            ? path
            : std::filesystem::absolute(path, pathError);
        if (pathError)
        {
            return FileIoFailure(
                source,
                "The options file path could not be resolved",
                pathError);
        }

        target = target.lexically_normal();
        if (!target.has_filename())
        {
            return FileIoFailure(
                source,
                "The options store path must name a file.");
        }

        return WriteOptionsFileNative(
            target,
            contents,
            source);
    }

    OptionsSubscription WatchOptionsFile(
        std::filesystem::path path,
        std::chrono::milliseconds pollingInterval,
        std::function<bool()> callback,
        std::shared_ptr<OptionsFileWatchState> appliedState,
        std::shared_ptr<std::mutex> fileMutex)
    {
        if (!callback)
        {
            return {};
        }

        auto state = std::make_shared<FileWatchState>(
            std::move(path),
            pollingInterval,
            std::move(callback),
            std::move(appliedState),
            std::move(fileMutex));
        state->Start();

        return OptionsSubscription(
            [state = std::move(state)] noexcept
            {
                state->Stop();
            });
    }
}
