#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "TGE/Core.hpp"

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace TGE::Tests::OptionsFixtures
{
    struct NestedOptions
    {
        bool enabled { true };
        std::vector<int> values { 1, 2 };

        bool operator==(const NestedOptions&) const = default;
    };

    struct TestOptions
    {
        std::string name { "default" };
        int count { 4 };
        std::uint64_t capacity { 64 };
        std::optional<std::string> tag;
        NestedOptions nested;

        bool operator==(const TestOptions&) const = default;
    };

    struct OtherOptions
    {
        bool active { false };
    };

    struct InvariantOptions
    {
        std::uint64_t left {};
        std::uint64_t right {};
    };

    struct FailingSaveOptions
    {
        int count { 1 };
        bool rejectSerialization { false };
    };

    struct FailingSaveDocument
    {
        int count { 1 };
    };

    class EncapsulatedOptions
    {
    public:
        EncapsulatedOptions() = default;

        EncapsulatedOptions(std::string endpointValue, int retriesValue)
            : endpoint(std::move(endpointValue)),
              retries(retriesValue)
        {
        }

        [[nodiscard]] const std::string& Endpoint() const noexcept
        {
            return endpoint;
        }

        [[nodiscard]] int Retries() const noexcept
        {
            return retries;
        }

        bool operator==(const EncapsulatedOptions&) const = default;

    private:
        std::string endpoint { "localhost" };
        int retries { 3 };
    };

    struct EncapsulatedOptionsDto
    {
        std::string endpoint { "localhost" };
        int retries { 3 };
    };
}

using TGE::Tests::OptionsFixtures::EncapsulatedOptions;
using TGE::Tests::OptionsFixtures::EncapsulatedOptionsDto;
using TGE::Tests::OptionsFixtures::FailingSaveDocument;
using TGE::Tests::OptionsFixtures::FailingSaveOptions;
using TGE::Tests::OptionsFixtures::InvariantOptions;
using TGE::Tests::OptionsFixtures::NestedOptions;
using TGE::Tests::OptionsFixtures::OtherOptions;
using TGE::Tests::OptionsFixtures::TestOptions;

namespace
{
    class DirectProvider final : public TGE::IOptionsProvider<TestOptions>
    {
    public:
        std::string_view Name() const noexcept override
        {
            return "database test provider";
        }

        TGE::OptionsResult<void> Apply(TestOptions& candidate) const override
        {
            candidate.name = "database";
            candidate.capacity = 512;
            return {};
        }
    };

    class ThrowingProvider final : public TGE::IOptionsProvider<TestOptions>
    {
    public:
        std::string_view Name() const noexcept override
        {
            return "throwing test provider";
        }

        TGE::OptionsResult<void> Apply(TestOptions&) const override
        {
            throw std::runtime_error("provider implementation failed");
        }
    };

    class NotifyingThrowingWatchProvider final
        : public TGE::IOptionsProvider<TestOptions>
    {
    public:
        std::string_view Name() const noexcept override
        {
            return "throwing watch test provider";
        }

        TGE::OptionsResult<void> Apply(TestOptions& candidate) const override
        {
            candidate.count = 77;
            return {};
        }

        TGE::OptionsSubscription Watch(ChangeCallback callback) override
        {
            callback();
            throw std::runtime_error("watch setup failed");
        }
    };

    class InitialApplyBarrierProvider final
        : public TGE::IOptionsProvider<TestOptions>
    {
    public:
        InitialApplyBarrierProvider()
            : initialApplyReached(
                  initialApplyReachedPromise.get_future().share()),
              releaseInitialApply(
                  releaseInitialApplyPromise.get_future().share())
        {
            value.count = 1;
        }

        std::string_view Name() const noexcept override
        {
            return "initial apply barrier";
        }

        TGE::OptionsResult<void> Apply(
            TestOptions& candidate) const override
        {
            {
                std::scoped_lock lock(mutex);
                candidate = value;
            }

            if (!firstApply.exchange(true))
            {
                initialApplyReachedPromise.set_value();
                releaseInitialApply.wait();
            }
            return {};
        }

        TGE::OptionsSubscription Watch(ChangeCallback callback) override
        {
            std::scoped_lock lock(mutex);
            changeCallback = std::move(callback);
            return {};
        }

        [[nodiscard]] bool WaitForInitialApply(
            std::chrono::milliseconds timeout) const
        {
            return initialApplyReached.wait_for(timeout) ==
                std::future_status::ready;
        }

        [[nodiscard]] bool SetCount(int count)
        {
            ChangeCallback callback;
            {
                std::scoped_lock lock(mutex);
                value.count = count;
                callback = changeCallback;
            }
            return callback ? callback() : true;
        }

        void ReleaseInitialApply()
        {
            releaseInitialApplyPromise.set_value();
        }

    private:
        mutable std::mutex mutex;
        TestOptions value;
        ChangeCallback changeCallback;
        mutable std::atomic<bool> firstApply { false };
        mutable std::promise<void> initialApplyReachedPromise;
        std::shared_future<void> initialApplyReached;
        std::promise<void> releaseInitialApplyPromise;
        std::shared_future<void> releaseInitialApply;
    };

    class ObservableJsonProvider final
        : public TGE::IOptionsProvider<TestOptions>
    {
    public:
        ObservableJsonProvider()
            : state(std::make_shared<State>())
        {
        }

        std::string_view Name() const noexcept override
        {
            return "observable JSON test provider";
        }

        TGE::OptionsResult<void> Apply(TestOptions& candidate) const override
        {
            std::string patch;
            {
                std::scoped_lock lock(state->mutex);
                patch = state->patch;
            }
            return TGE::DeserializeOptionsInto(candidate, patch, Name());
        }

        TGE::OptionsSubscription Watch(ChangeCallback callback) override
        {
            {
                std::scoped_lock lock(state->mutex);
                state->callback = std::move(callback);
            }

            std::weak_ptr<State> weakState = state;
            return TGE::OptionsSubscription(
                [weakState] noexcept
                {
                    if (auto current = weakState.lock())
                    {
                        std::scoped_lock lock(current->mutex);
                        current->callback = {};
                    }
                });
        }

        void SetPatch(std::string patch)
        {
            ChangeCallback callback;
            {
                std::scoped_lock lock(state->mutex);
                state->patch = std::move(patch);
                callback = state->callback;
            }

            if (callback)
            {
                callback();
            }
        }

    private:
        struct State
        {
            std::mutex mutex;
            std::string patch { R"({"count":6})" };
            ChangeCallback callback;
        };

        std::shared_ptr<State> state;
    };

    class TemporaryOptionsFile
    {
    public:
        TemporaryOptionsFile()
        {
            static std::atomic<std::uint64_t> nextId {};
            path = std::filesystem::temp_directory_path() /
                std::format(
                    "tge-options-{}-{}.json",
                    std::chrono::steady_clock::now()
                        .time_since_epoch()
                        .count(),
                    nextId.fetch_add(1));
        }

        ~TemporaryOptionsFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }

        void Write(std::string_view contents) const
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << contents;
            stream.close();
            ASSERT_TRUE(stream.good());
        }

        [[nodiscard]] std::string Read() const
        {
            std::ifstream stream(path, std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char> {});
        }

        [[nodiscard]] std::size_t TemporarySiblingCount() const
        {
            const auto temporaryPrefix =
                path.filename().string() + ".tmp.";
            const auto backupPrefix =
                path.filename().string() + ".backup.";
            std::size_t count = 0;
            for (const auto& entry :
                 std::filesystem::directory_iterator(path.parent_path()))
            {
                const auto filename =
                    entry.path().filename().string();
                if (filename.starts_with(temporaryPrefix) ||
                    filename.starts_with(backupPrefix))
                {
                    ++count;
                }
            }
            return count;
        }

        std::filesystem::path path;
    };

    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(std::string name, std::string value)
            : name(std::move(name))
        {
#if defined(_WIN32)
            _putenv_s(this->name.c_str(), value.c_str());
#else
            setenv(this->name.c_str(), value.c_str(), 1);
#endif
        }

        ~ScopedEnvironmentVariable()
        {
#if defined(_WIN32)
            _putenv_s(name.c_str(), "");
#else
            unsetenv(name.c_str());
#endif
        }

    private:
        std::string name;
    };
}

template<>
struct TGE::OptionsSerializer<EncapsulatedOptions>
{
    static TGE::OptionsResult<std::string> Serialize(
        const EncapsulatedOptions& value,
        bool pretty = false)
    {
        return TGE::SerializeOptions(
            EncapsulatedOptionsDto {
                .endpoint = value.Endpoint(),
                .retries = value.Retries()
            },
            pretty);
    }

    static TGE::OptionsResult<void> DeserializeInto(
        EncapsulatedOptions& value,
        std::string_view serialized)
    {
        EncapsulatedOptionsDto candidate {
            .endpoint = value.Endpoint(),
            .retries = value.Retries()
        };
        auto result =
            TGE::DeserializeOptionsInto(candidate, serialized);
        if (result)
        {
            value = EncapsulatedOptions(
                std::move(candidate.endpoint),
                candidate.retries);
        }
        return result;
    }
};

template<>
struct TGE::OptionsSerializer<FailingSaveOptions>
{
    static TGE::OptionsResult<std::string> Serialize(
        const FailingSaveOptions& value,
        bool pretty = false)
    {
        if (value.rejectSerialization)
        {
            return std::unexpected(TGE::OptionsError {
                .code = TGE::OptionsErrorCode::Serialization,
                .source = {},
                .message = "Serialization was rejected for testing."
            });
        }

        return TGE::SerializeOptions(
            FailingSaveDocument { .count = value.count },
            pretty);
    }

    static TGE::OptionsResult<void> DeserializeInto(
        FailingSaveOptions& value,
        std::string_view serialized)
    {
        FailingSaveDocument document { .count = value.count };
        auto result = TGE::DeserializeOptionsInto(document, serialized);
        if (result)
        {
            value.count = document.count;
        }
        return result;
    }
};

static_assert(std::derived_from<
    TGE::JsonFileOptionsProvider<TestOptions>,
    TGE::IOptionsStore<TestOptions>>);
static_assert(!std::derived_from<
    TGE::EnvironmentOptionsProvider<TestOptions>,
    TGE::IOptionsStore<TestOptions>>);
static_assert(!std::derived_from<
    TGE::MemoryOptionsProvider<TestOptions>,
    TGE::IOptionsStore<TestOptions>>);

TEST(OptionsSerializationTests, RoundTripsOwningAggregates)
{
    TestOptions expected {
        .name = "escaped \"value\"\n",
        .count = 12,
        .capacity = 9'007'199'254'740'993ULL,
        .tag = "present",
        .nested = {
            .enabled = false,
            .values = { 3, 5, 8 }
        }
    };

    auto serialized = TGE::SerializeOptions(expected);
    ASSERT_TRUE(serialized) << serialized.error().message;

    auto actual = TGE::DeserializeOptions<TestOptions>(*serialized);
    ASSERT_TRUE(actual) << actual.error().message;
    EXPECT_EQ(*actual, expected);

    auto serializedAgain = TGE::SerializeOptions(*actual);
    ASSERT_TRUE(serializedAgain);
    EXPECT_EQ(*serializedAgain, *serialized);
}

TEST(OptionsSerializationTests, SupportsEncapsulatedCustomization)
{
    const EncapsulatedOptions expected("service.internal", 7);

    auto serialized = TGE::SerializeOptions(expected);
    ASSERT_TRUE(serialized) << serialized.error().message;

    auto actual =
        TGE::DeserializeOptions<EncapsulatedOptions>(*serialized);
    ASSERT_TRUE(actual) << actual.error().message;
    EXPECT_EQ(*actual, expected);

    auto patched = TGE::DeserializeOptionsInto(
        *actual,
        R"({"retries":11})");
    ASSERT_TRUE(patched) << patched.error().message;
    EXPECT_EQ(actual->Endpoint(), "service.internal");
    EXPECT_EQ(actual->Retries(), 11);
}

TEST(OptionsSerializationTests, PartialJsonPreservesExistingValues)
{
    TestOptions options;

    auto result = TGE::DeserializeOptionsInto(
        options,
        R"({"count":9,"nested":{"enabled":false}})");

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(options.name, "default");
    EXPECT_EQ(options.count, 9);
    EXPECT_FALSE(options.nested.enabled);
    EXPECT_EQ(options.nested.values, (std::vector<int> { 1, 2 }));
}

TEST(OptionsSerializationTests, RejectsMalformedUnknownAndWrongTypedValues)
{
    TestOptions options;

    const auto malformed =
        TGE::DeserializeOptionsInto(options, R"({"count":)");
    ASSERT_FALSE(malformed);
    EXPECT_EQ(
        malformed.error().code,
        TGE::OptionsErrorCode::Serialization);

    const auto unknown =
        TGE::DeserializeOptionsInto(options, R"({"unknown":1})");
    ASSERT_FALSE(unknown);
    EXPECT_THAT(unknown.error().message, testing::HasSubstr("unknown_key"));

    const auto wrongType =
        TGE::DeserializeOptionsInto(options, R"({"count":"many"})");
    EXPECT_FALSE(wrongType);

    const auto overflow = TGE::DeserializeOptionsInto(
        options,
        R"({"count":999999999999999999999})");
    EXPECT_FALSE(overflow);
}

TEST(OptionsProviderTests, AppliesSourcesInRegistrationOrder)
{
    TGE::ServiceCollection services;
    auto first = std::make_shared<ObservableJsonProvider>();
    first->SetPatch(R"({"count":7,"name":"first"})");

    auto builder = services.AddOptions<TestOptions>();
    builder
        .AddProvider(first)
        .AddProvider(std::make_shared<DirectProvider>())
        .Configure(
            [](TestOptions& value)
            {
                value.count = 11;
            },
            "final override");

    auto current = builder.GetMonitor()->Current();
    EXPECT_EQ(current->name, "database");
    EXPECT_EQ(current->count, 11);
    EXPECT_EQ(current->capacity, 512u);
    EXPECT_EQ(current->nested.values, (std::vector<int> { 1, 2 }));
}

TEST(OptionsProviderTests, ReadsOptionalAndRequiredJsonFiles)
{
    TemporaryOptionsFile file;

    TGE::ServiceCollection optionalServices;
    EXPECT_NO_THROW(
        optionalServices.AddOptions<TestOptions>().FromJsonFile(
            file.path,
            { .optional = true }));

    TGE::ServiceCollection requiredServices;
    EXPECT_THROW(
        requiredServices.AddOptions<TestOptions>().FromJsonFile(file.path),
        TGE::OptionsException);

    file.Write(R"({"name":"file","count":21})");
    TGE::ServiceCollection configuredServices;
    auto builder = configuredServices.AddOptions<TestOptions>();
    EXPECT_NO_THROW(builder.FromJsonFile(file.path));
    EXPECT_EQ(builder.GetMonitor()->Current()->name, "file");
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 21);
}

TEST(OptionsStoreTests, JsonStoreCreatesAndRoundTripsACompleteDocument)
{
    TemporaryOptionsFile file;
    auto json =
        std::make_shared<TGE::JsonFileOptionsProvider<TestOptions>>(
            file.path,
            TGE::JsonFileOptionsProviderSettings { .optional = true });
    std::shared_ptr<TGE::IOptionsStore<TestOptions>> store = json;

    const TestOptions expected {
        .name = "saved",
        .count = 37,
        .capacity = 4'096,
        .tag = "persistent",
        .nested = {
            .enabled = false,
            .values = { 2, 3, 5, 7 }
        }
    };

    auto saved = store->Save(expected);
    ASSERT_TRUE(saved) << saved.error().message;
    EXPECT_TRUE(std::filesystem::is_regular_file(file.path));
    EXPECT_EQ(file.TemporarySiblingCount(), 0u);
#if !defined(_WIN32)
    constexpr auto PermissionBits =
        std::filesystem::perms::owner_all |
        std::filesystem::perms::group_all |
        std::filesystem::perms::others_all;
    EXPECT_EQ(
        std::filesystem::status(file.path).permissions() &
            PermissionBits,
        std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write);
#endif

    auto deserialized =
        TGE::DeserializeOptions<TestOptions>(file.Read(), json->Name());
    ASSERT_TRUE(deserialized) << deserialized.error().message;
    EXPECT_EQ(*deserialized, expected);

    TestOptions applied;
    auto appliedResult = json->Apply(applied);
    ASSERT_TRUE(appliedResult) << appliedResult.error().message;
    EXPECT_EQ(applied, expected);
}

TEST(OptionsStoreTests, ConcurrentSavesPublishOneCompleteDocument)
{
    TemporaryOptionsFile file;
    auto json =
        std::make_shared<TGE::JsonFileOptionsProvider<TestOptions>>(
            file.path,
            TGE::JsonFileOptionsProviderSettings { .optional = true });
    const TestOptions first {
        .name = std::string(32'768, 'A'),
        .count = 1,
        .capacity = 1'024,
        .tag = "first",
        .nested = {
            .enabled = true,
            .values = { 1, 1, 2, 3, 5 }
        }
    };
    const TestOptions second {
        .name = std::string(32'768, 'B'),
        .count = 2,
        .capacity = 2'048,
        .tag = "second",
        .nested = {
            .enabled = false,
            .values = { 8, 13, 21 }
        }
    };
    std::atomic<int> failures {};

    std::jthread firstWriter(
        [&]
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                if (!json->Save(first))
                {
                    failures.fetch_add(1);
                }
            }
        });
    std::jthread secondWriter(
        [&]
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                if (!json->Save(second))
                {
                    failures.fetch_add(1);
                }
            }
        });
    firstWriter.join();
    secondWriter.join();

    EXPECT_EQ(failures.load(), 0);
    auto persisted =
        TGE::DeserializeOptions<TestOptions>(file.Read());
    ASSERT_TRUE(persisted) << persisted.error().message;
    EXPECT_TRUE(*persisted == first || *persisted == second);
    EXPECT_EQ(file.TemporarySiblingCount(), 0u);
}

#if !defined(_WIN32)
TEST(OptionsStoreTests, ReplacingAFilePreservesPosixPermissionBits)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":1})");
    constexpr auto ExpectedPermissions =
        std::filesystem::perms::owner_read |
        std::filesystem::perms::owner_write |
        std::filesystem::perms::group_read;
    std::error_code permissionError;
    std::filesystem::permissions(
        file.path,
        ExpectedPermissions,
        std::filesystem::perm_options::replace,
        permissionError);
    ASSERT_FALSE(permissionError) << permissionError.message();

    TGE::JsonFileOptionsProvider<TestOptions> json(file.path);
    ASSERT_TRUE(json.Save(TestOptions {}));

    constexpr auto PermissionBits =
        std::filesystem::perms::owner_all |
        std::filesystem::perms::group_all |
        std::filesystem::perms::others_all;
    EXPECT_EQ(
        std::filesystem::status(file.path).permissions() &
            PermissionBits,
        ExpectedPermissions);
}
#endif

TEST(OptionsStoreTests, SerializationFailurePreservesTheExistingDocument)
{
    TemporaryOptionsFile file;
    file.Write("original bytes");
    TGE::JsonFileOptionsProvider<FailingSaveOptions> json(file.path);

    auto saved = json.Save(FailingSaveOptions {
        .count = 42,
        .rejectSerialization = true
    });

    ASSERT_FALSE(saved);
    EXPECT_EQ(
        saved.error().code,
        TGE::OptionsErrorCode::Serialization);
    EXPECT_EQ(saved.error().source, json.Name());
    EXPECT_EQ(file.Read(), "original bytes");
    EXPECT_EQ(file.TemporarySiblingCount(), 0u);
}

TEST(OptionsStoreTests, ReplacementFailurePreservesTheDestination)
{
    TemporaryOptionsFile destination;
    std::error_code directoryError;
    ASSERT_TRUE(std::filesystem::create_directory(
        destination.path,
        directoryError));
    ASSERT_FALSE(directoryError) << directoryError.message();

    TGE::JsonFileOptionsProvider<TestOptions> json(destination.path);
    auto saved = json.Save(TestOptions {});

    ASSERT_FALSE(saved);
    EXPECT_EQ(saved.error().code, TGE::OptionsErrorCode::Io);
    EXPECT_EQ(saved.error().source, json.Name());
    EXPECT_TRUE(std::filesystem::is_directory(destination.path));
    EXPECT_EQ(destination.TemporarySiblingCount(), 0u);
}

TEST(OptionsStoreTests, SavingDoesNotChangeProviderPrecedence)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":5})");
    ScopedEnvironmentVariable environmentCount(
        "TGE_STORE_PRIORITY__COUNT",
        "99");

    auto json =
        std::make_shared<TGE::JsonFileOptionsProvider<TestOptions>>(
            file.path);
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder
        .AddProvider(json)
        .FromEnvironment("TGE_STORE_PRIORITY");

    ASSERT_EQ(builder.GetMonitor()->Current()->count, 99);

    TestOptions persisted;
    persisted.count = 7;
    ASSERT_TRUE(json->Save(persisted));
    ASSERT_TRUE(builder.GetMonitor()->Reload());
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 99);

    TestOptions fileLayer;
    ASSERT_TRUE(json->Apply(fileLayer));
    EXPECT_EQ(fileLayer.count, 7);
}

TEST(OptionsStoreTests, SaveWithoutWatchingPersistsUntilExplicitReload)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":5})");

    auto json =
        std::make_shared<TGE::JsonFileOptionsProvider<TestOptions>>(
            file.path);
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder.AddProvider(json);

    TestOptions persisted;
    persisted.count = 8;
    ASSERT_TRUE(json->Save(persisted));
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 5);

    ASSERT_TRUE(builder.GetMonitor()->Reload());
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 8);
}

TEST(OptionsProviderTests, ReadsTypedAndNestedEnvironmentValues)
{
    ScopedEnvironmentVariable name(
        "TGE_OPTIONS_TEST__NAME",
        "environment");
    ScopedEnvironmentVariable count(
        "TGE_OPTIONS_TEST__COUNT",
        "17");
    ScopedEnvironmentVariable enabled(
        "TGE_OPTIONS_TEST__NESTED__ENABLED",
        "false");

    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder.FromEnvironment("TGE_OPTIONS_TEST");

    const auto current = builder.GetMonitor()->Current();
    EXPECT_EQ(current->name, "environment");
    EXPECT_EQ(current->count, 17);
    EXPECT_FALSE(current->nested.enabled);
    EXPECT_EQ(current->nested.values, (std::vector<int> { 1, 2 }));
}

TEST(OptionsProviderTests, RejectsConflictingEnvironmentPaths)
{
    ScopedEnvironmentVariable scalar(
        "TGE_OPTIONS_CONFLICT__NESTED",
        "false");
    ScopedEnvironmentVariable child(
        "TGE_OPTIONS_CONFLICT__NESTED__ENABLED",
        "true");

    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();

    try
    {
        builder.FromEnvironment("TGE_OPTIONS_CONFLICT");
        FAIL() << "A scalar and nested environment path must conflict.";
    }
    catch (const TGE::OptionsException& exception)
    {
        EXPECT_EQ(
            exception.Error().code,
            TGE::OptionsErrorCode::Provider);
        EXPECT_THAT(
            exception.Error().message,
            testing::HasSubstr("conflicts"));
    }

    ASSERT_TRUE(builder.GetMonitor()->Reload());
    EXPECT_EQ(builder.GetMonitor()->Current()->nested, NestedOptions {});
}

TEST(OptionsProviderTests, ObservableMemoryOverridesReloadAllConsumers)
{
    TGE::ServiceCollection services;
    auto memory =
        std::make_shared<TGE::MemoryOptionsProvider<TestOptions>>();
    auto builder = services.AddOptions<TestOptions>();
    builder
        .Configure(
            [](TestOptions& options)
            {
                options.count = 7;
            })
        .AddProvider(memory);

    TestOptions override;
    override.name = "memory";
    override.count = 42;
    EXPECT_TRUE(memory->Set(override));

    EXPECT_EQ(builder.GetMonitor()->Current()->name, "memory");
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 42);

    EXPECT_TRUE(memory->Clear());
    EXPECT_EQ(builder.GetMonitor()->Current()->name, "default");
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 7);
}

TEST(OptionsProviderTests, MemoryProviderReportsRejectedReloads)
{
    TGE::ServiceCollection services;
    auto memory =
        std::make_shared<TGE::MemoryOptionsProvider<TestOptions>>();
    auto builder = services.AddOptions<TestOptions>();
    builder
        .AddProvider(memory)
        .Validate(
            [](const TestOptions& options)
            {
                return options.count > 0;
            },
            "count must be positive");

    TestOptions invalid;
    invalid.count = 0;
    EXPECT_FALSE(memory->Set(invalid));
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 4);

    invalid.count = 2;
    EXPECT_TRUE(memory->Set(std::move(invalid)));
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 2);
}

TEST(OptionsProviderTests, ProviderExceptionsBecomeTransactionalErrors)
{
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();

    try
    {
        builder.AddProvider(std::make_shared<ThrowingProvider>());
        FAIL() << "A throwing provider must fail registration.";
    }
    catch (const TGE::OptionsException& exception)
    {
        EXPECT_EQ(
            exception.Error().code,
            TGE::OptionsErrorCode::Provider);
        EXPECT_EQ(
            exception.Error().source,
            "throwing test provider");
        EXPECT_THAT(
            exception.Error().message,
            testing::HasSubstr("provider implementation failed"));
    }

    EXPECT_EQ(*builder.GetMonitor()->Current(), TestOptions {});
    ASSERT_TRUE(builder.GetMonitor()->Reload());
    EXPECT_FALSE(builder.GetMonitor()->LastError());
}

TEST(OptionsProviderTests, WatchFailureCannotPublishATentativeSource)
{
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    const auto previous = builder.GetMonitor()->Current();

    EXPECT_THROW(
        builder.AddProvider(
            std::make_shared<NotifyingThrowingWatchProvider>()),
        TGE::OptionsException);

    EXPECT_EQ(builder.GetMonitor()->Current(), previous);
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 4);
    ASSERT_TRUE(builder.GetMonitor()->Reload());
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 4);
}

TEST(OptionsProviderTests, SubscribesBeforeInitialApplyClosesHandoff)
{
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    auto provider = std::make_shared<InitialApplyBarrierProvider>();
    std::exception_ptr registrationError;

    std::jthread registration(
        [&]
        {
            try
            {
                builder.AddProvider(provider);
            }
            catch (...)
            {
                registrationError = std::current_exception();
            }
        });

    const bool reachedApply = provider->WaitForInitialApply(
        std::chrono::seconds(2));
    const bool acceptedDuringRegistration = provider->SetCount(42);
    provider->ReleaseInitialApply();
    registration.join();

    EXPECT_TRUE(reachedApply);
    EXPECT_FALSE(acceptedDuringRegistration);
    EXPECT_FALSE(registrationError);
    EXPECT_EQ(builder.GetMonitor()->Current()->count, 42);
}

TEST(OptionsMonitorTests, PublishesOneSnapshotToEveryConsumer)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    std::shared_ptr<TGE::IOptionsMonitor<TestOptions>> first = monitor;
    std::shared_ptr<TGE::IOptionsMonitor<TestOptions>> second = monitor;

    std::uint64_t observedVersion {};
    auto subscription = first->OnChange(
        [&](const TGE::OptionsChange<TestOptions>& change)
        {
            observedVersion = change.version;
            EXPECT_EQ(change.previous->count, 4);
            EXPECT_EQ(change.current->count, 25);
            EXPECT_EQ(second->Current(), change.current);
        });

    auto updated = monitor->Update(
        [](TestOptions& value)
        {
            value.count = 25;
        });

    ASSERT_TRUE(updated);
    EXPECT_EQ(first->Current(), second->Current());
    EXPECT_EQ(first->Current()->count, 25);
    EXPECT_EQ(observedVersion, *updated);

    const auto snapshot = first->CurrentSnapshot();
    EXPECT_EQ(snapshot->count, 25);
    EXPECT_EQ(snapshot.version, *updated);
    EXPECT_EQ(snapshot.value, first->Current());
}

TEST(OptionsMonitorTests, ObserveInitializesBeforeConcurrentPublication)
{
    auto monitor =
        std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    std::promise<void> initialEntered;
    std::promise<void> releaseInitial;
    auto release = releaseInitial.get_future().share();
    std::mutex observedMutex;
    std::vector<std::uint64_t> observedVersions;
    std::shared_ptr<const TestOptions> cached;
    bool initialPreviousWasEmpty = false;
    bool changedPreviousWasCurrent = false;
    TGE::OptionsSubscription subscription;

    std::jthread observer(
        [&]
        {
            subscription = monitor->Observe(
                [&](const TGE::OptionsChange<TestOptions>& change)
                {
                    {
                        std::scoped_lock lock(observedMutex);
                        observedVersions.emplace_back(change.version);
                        cached = change.current;
                        if (change.version == 0)
                        {
                            initialPreviousWasEmpty = !change.previous;
                        }
                        else
                        {
                            changedPreviousWasCurrent =
                                change.previous &&
                                change.previous->count == 4;
                        }
                    }

                    if (change.version == 0)
                    {
                        initialEntered.set_value();
                        release.wait();
                    }
                });
        });

    initialEntered.get_future().wait();
    TestOptions changed;
    changed.count = 19;
    auto updated = monitor->Set(std::move(changed));
    ASSERT_TRUE(updated);
    releaseInitial.set_value();
    observer.join();

    std::scoped_lock lock(observedMutex);
    EXPECT_EQ(
        observedVersions,
        (std::vector<std::uint64_t> { 0, 1 }));
    ASSERT_TRUE(cached);
    EXPECT_EQ(cached->count, 19);
    EXPECT_TRUE(initialPreviousWasEmpty);
    EXPECT_TRUE(changedPreviousWasCurrent);
}

TEST(OptionsMonitorTests, DeduplicatesValuesAndUnsubscribesWithTokenLifetime)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    int calls = 0;

    {
        auto subscription = monitor->OnChange(
            [&](const auto&) { ++calls; });

        ASSERT_TRUE(monitor->Set(TestOptions {}));
        EXPECT_EQ(monitor->Version(), 0u);
        EXPECT_EQ(calls, 0);

        TestOptions changed;
        changed.count = 8;
        ASSERT_TRUE(monitor->Set(changed));
        EXPECT_EQ(calls, 1);
    }

    TestOptions changedAgain;
    changedAgain.count = 9;
    ASSERT_TRUE(monitor->Set(changedAgain));
    EXPECT_EQ(calls, 1);
}

TEST(OptionsMonitorTests, InvokesCallbacksOutsideLocksAndIsolatesExceptions)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    bool reentrantReadSucceeded = false;
    bool laterCallbackRan = false;

    auto throwing = monitor->OnChange(
        [&](const auto&)
        {
            reentrantReadSucceeded = monitor->Current()->count == 10;
            throw std::runtime_error("observer failed");
        });
    auto later = monitor->OnChange(
        [&](const auto&) { laterCallbackRan = true; });

    TestOptions changed;
    changed.count = 10;
    ASSERT_TRUE(monitor->Set(changed));

    EXPECT_TRUE(reentrantReadSucceeded);
    EXPECT_TRUE(laterCallbackRan);
}

TEST(OptionsMonitorTests, SerializesReentrantNotificationsByVersion)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    std::vector<std::uint64_t> observedVersions;
    int callbackDepth = 0;
    bool nestedCallback = false;

    auto subscription = monitor->OnChange(
        [&](const TGE::OptionsChange<TestOptions>& change)
        {
            ++callbackDepth;
            nestedCallback = nestedCallback || callbackDepth > 1;
            observedVersions.emplace_back(change.version);

            if (change.version == 1)
            {
                auto nested = monitor->Update(
                    [](TestOptions& options)
                    {
                        options.count = 12;
                    });
                EXPECT_TRUE(nested);
                EXPECT_EQ(*nested, 2u);
            }

            --callbackDepth;
        });

    auto first = monitor->Update(
        [](TestOptions& options)
        {
            options.count = 8;
        });

    ASSERT_TRUE(first);
    EXPECT_FALSE(nestedCallback);
    EXPECT_EQ(
        observedVersions,
        (std::vector<std::uint64_t> { 1, 2 }));
}

TEST(OptionsMonitorTests, ResetWaitsForAnInFlightCallback)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    std::promise<void> callbackEntered;
    std::promise<void> releaseCallback;
    auto release = releaseCallback.get_future().share();
    std::atomic<int> calls {};

    auto subscription = monitor->OnChange(
        [&](const auto&)
        {
            calls.fetch_add(1);
            callbackEntered.set_value();
            release.wait();
        });

    std::jthread writer(
        [&]
        {
            TestOptions changed;
            changed.count = 9;
            EXPECT_TRUE(monitor->Set(changed));
        });
    callbackEntered.get_future().wait();

    std::promise<void> resetStarted;
    std::promise<void> resetFinished;
    auto resetFinishedFuture = resetFinished.get_future();
    std::jthread resetter(
        [&]
        {
            resetStarted.set_value();
            subscription.Reset();
            resetFinished.set_value();
        });
    resetStarted.get_future().wait();

    EXPECT_EQ(
        resetFinishedFuture.wait_for(std::chrono::milliseconds(20)),
        std::future_status::timeout);

    releaseCallback.set_value();
    resetFinishedFuture.wait();
    writer.join();
    resetter.join();

    TestOptions changedAgain;
    changedAgain.count = 10;
    ASSERT_TRUE(monitor->Set(changedAgain));
    EXPECT_EQ(calls.load(), 1);
}

TEST(OptionsMonitorTests, CoalescesPendingChangesForSlowObservers)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    std::promise<void> firstCallbackEntered;
    std::promise<void> releaseFirstCallback;
    auto release = releaseFirstCallback.get_future().share();
    std::mutex observedMutex;
    std::vector<std::uint64_t> observed;

    auto subscription = monitor->OnChange(
        [&](const TGE::OptionsChange<TestOptions>& change)
        {
            {
                std::scoped_lock lock(observedMutex);
                observed.emplace_back(change.version);
            }

            if (change.version == 1)
            {
                firstCallbackEntered.set_value();
                release.wait();
            }
        });

    std::jthread firstWriter(
        [&]
        {
            TestOptions changed;
            changed.count = 1;
            static_cast<void>(monitor->Set(std::move(changed)));
        });
    firstCallbackEntered.get_future().wait();

    for (int count = 2; count <= 50; ++count)
    {
        TestOptions changed;
        changed.count = count;
        ASSERT_TRUE(monitor->Set(std::move(changed)));
    }

    releaseFirstCallback.set_value();
    firstWriter.join();

    std::scoped_lock lock(observedMutex);
    EXPECT_EQ(
        observed,
        (std::vector<std::uint64_t> { 1, 50 }));
    EXPECT_EQ(monitor->Current()->count, 50);
    EXPECT_EQ(monitor->Version(), 50u);
}

TEST(OptionsMonitorTests, SurvivesItsFinalOwnerBeingReleasedByACallback)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    std::weak_ptr<TGE::OptionsMonitor<TestOptions>> weakMonitor = monitor;
    auto* authority = monitor.get();
    auto subscription = monitor->OnChange(
        [&](const auto&)
        {
            monitor.reset();
        });

    TestOptions changed;
    changed.count = 10;
    auto result = authority->Set(std::move(changed));

    ASSERT_TRUE(result);
    EXPECT_TRUE(weakMonitor.expired());
}

TEST(OptionsMonitorTests, InvalidReloadRetainsLastKnownGoodSnapshot)
{
    TGE::ServiceCollection services;
    auto source = std::make_shared<ObservableJsonProvider>();
    auto builder = services.AddOptions<TestOptions>();
    builder.AddProvider(source);

    auto monitor = builder.GetMonitor();
    const auto good = monitor->Current();
    ASSERT_EQ(good->count, 6);

    source->SetPatch(R"({"count":"invalid"})");
    EXPECT_EQ(monitor->Current(), good);
    ASSERT_TRUE(monitor->LastError());
    EXPECT_EQ(
        monitor->LastError()->code,
        TGE::OptionsErrorCode::Serialization);

    source->SetPatch(R"({"count":18})");
    EXPECT_EQ(monitor->Current()->count, 18);
    EXPECT_FALSE(monitor->LastError());
}

TEST(OptionsMonitorTests, ValidationRejectsRuntimeUpdatesTransactionally)
{
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder.Validate(
        [](const TestOptions& value)
        {
            return value.count > 0;
        },
        "count must be positive");

    auto monitor = builder.GetMonitor();
    const auto previous = monitor->Current();

    auto invalid = monitor->Update(
        [](TestOptions& value)
        {
            value.count = 0;
        });
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, TGE::OptionsErrorCode::Validation);
    EXPECT_EQ(monitor->Current(), previous);

    auto valid = monitor->Update(
        [](TestOptions& value)
        {
            value.count = 2;
        });
    ASSERT_TRUE(valid);
    EXPECT_EQ(monitor->Current()->count, 2);
    EXPECT_FALSE(monitor->LastError());
}

TEST(OptionsMonitorTests, PropagatesExpectedMutatorFailures)
{
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    const auto previous = monitor->Current();

    auto failed = monitor->Update(
        [](TestOptions& options) -> TGE::OptionsResult<void>
        {
            options.count = 99;
            return std::unexpected(TGE::OptionsError {
                .code = TGE::OptionsErrorCode::Update,
                .source = {},
                .message = "update declined"
            });
        });

    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().source, "runtime update");
    EXPECT_EQ(monitor->Current(), previous);
    EXPECT_EQ(monitor->Current()->count, 4);
}

TEST(OptionsMonitorTests, FailedBuilderStepsAreRolledBack)
{
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();

    EXPECT_THROW(
        builder.Configure(
            [](TestOptions&)
            {
                throw std::runtime_error("configure failed");
            }),
        TGE::OptionsException);
    EXPECT_THROW(
        builder.Validate(
            [](const TestOptions&) { return false; },
            "validator failed"),
        TGE::OptionsException);
    EXPECT_THROW(
        builder.Configure(
            [](TestOptions&) -> TGE::OptionsResult<void>
            {
                return std::unexpected(TGE::OptionsError {
                    .code = TGE::OptionsErrorCode::Provider,
                    .source = {},
                    .message = "expected configure failure"
                });
            },
            "expected configure"),
        TGE::OptionsException);

    auto updated = builder.GetMonitor()->Update(
        [](TestOptions& options)
        {
            options.count = -1;
        });

    ASSERT_TRUE(updated);
    EXPECT_EQ(builder.GetMonitor()->Current()->count, -1);
    EXPECT_FALSE(builder.GetMonitor()->LastError());
}

TEST(OptionsMonitorTests, FileChangesReloadAllConsumers)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":5})");

    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder.FromJsonFile(
        file.path,
        {
            .optional = false,
            .reloadOnChange = true,
            .pollingInterval = std::chrono::milliseconds(20)
        });

    auto monitor = builder.GetMonitor();
    ASSERT_EQ(monitor->Current()->count, 5);

    std::mutex mutex;
    std::condition_variable changed;
    bool observed = false;
    auto subscription = monitor->OnChange(
        [&](const TGE::OptionsChange<TestOptions>& change)
        {
            if (change.current->count == 123)
            {
                {
                    std::scoped_lock lock(mutex);
                    observed = true;
                }
                changed.notify_all();
            }
        });

    file.Write(R"({"count":123})");

    std::unique_lock lock(mutex);
    EXPECT_TRUE(changed.wait_for(
        lock,
        std::chrono::seconds(2),
        [&] { return observed; }));
    EXPECT_EQ(monitor->Current()->count, 123);
}

TEST(OptionsStoreTests, WatchedSavesUseNormalValidationAndRecovery)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":5})");

    auto json =
        std::make_shared<TGE::JsonFileOptionsProvider<TestOptions>>(
            file.path,
            TGE::JsonFileOptionsProviderSettings {
                .optional = false,
                .reloadOnChange = true,
                .pollingInterval = std::chrono::milliseconds(20)
            });
    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder
        .AddProvider(json)
        .Validate(
            [](const TestOptions& options)
            {
                return options.count > 0;
            },
            "count must be positive");
    auto monitor = builder.GetMonitor();

    TestOptions accepted;
    accepted.count = 12;
    ASSERT_TRUE(json->Save(accepted));

    const auto acceptedDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (monitor->Current()->count != 12 &&
           std::chrono::steady_clock::now() < acceptedDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(monitor->Current()->count, 12);
    EXPECT_FALSE(monitor->LastError());

    TestOptions rejected;
    rejected.count = 0;
    ASSERT_TRUE(json->Save(rejected));

    const auto rejectedDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!monitor->LastError() &&
           std::chrono::steady_clock::now() < rejectedDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(monitor->LastError());
    EXPECT_EQ(
        monitor->LastError()->code,
        TGE::OptionsErrorCode::Validation);
    EXPECT_EQ(monitor->Current()->count, 12);

    accepted.count = 13;
    ASSERT_TRUE(json->Save(accepted));

    const auto recoveryDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((monitor->Current()->count != 13 ||
            monitor->LastError()) &&
           std::chrono::steady_clock::now() < recoveryDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(monitor->Current()->count, 13);
    EXPECT_FALSE(monitor->LastError());
}

TEST(OptionsMonitorTests, UnchangedFileDoesNotReplaceRuntimeUpdates)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":5})");

    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder.FromJsonFile(
        file.path,
        {
            .reloadOnChange = true,
            .pollingInterval = std::chrono::milliseconds(20)
        });

    auto monitor = builder.GetMonitor();
    TestOptions runtime = *monitor->Current();
    runtime.count = 99;
    ASSERT_TRUE(monitor->Set(std::move(runtime)));

    // Let several polling intervals elapse. Establishing the watch baseline
    // must not be mistaken for a file change.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(monitor->Current()->count, 99);

    file.Write(R"({"count":123})");
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (monitor->Current()->count != 123 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(monitor->Current()->count, 123);
}

TEST(OptionsMonitorTests, FileWatcherDetectsSameSizePreservedTimeChanges)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":111})");
    const auto originalWriteTime =
        std::filesystem::last_write_time(file.path);

    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder.FromJsonFile(
        file.path,
        {
            .reloadOnChange = true,
            .pollingInterval = std::chrono::milliseconds(20)
        });

    std::mutex mutex;
    std::condition_variable changed;
    bool observed = false;
    auto subscription = builder.GetMonitor()->OnChange(
        [&](const TGE::OptionsChange<TestOptions>& change)
        {
            if (change.current->count == 222)
            {
                {
                    std::scoped_lock lock(mutex);
                    observed = true;
                }
                changed.notify_all();
            }
        });

    file.Write(R"({"count":222})");
    std::error_code timeError;
    std::filesystem::last_write_time(
        file.path,
        originalWriteTime,
        timeError);
    ASSERT_FALSE(timeError) << timeError.message();

    std::unique_lock lock(mutex);
    EXPECT_TRUE(changed.wait_for(
        lock,
        std::chrono::seconds(2),
        [&] { return observed; }));
}

TEST(OptionsMonitorTests, FileWatcherRetriesRejectedSignatures)
{
    TemporaryOptionsFile file;
    file.Write("first");

    std::mutex mutex;
    std::condition_variable retried;
    int calls = 0;
    auto subscription = TGE::detail::WatchOptionsFile(
        file.path,
        std::chrono::milliseconds(20),
        [&]
        {
            std::scoped_lock lock(mutex);
            ++calls;
            retried.notify_all();
            return calls >= 2;
        });

    file.Write("other");

    std::unique_lock lock(mutex);
    EXPECT_TRUE(retried.wait_for(
        lock,
        std::chrono::seconds(2),
        [&] { return calls >= 2; }));
}

TEST(OptionsMonitorTests, FileWatcherSeedsMatchingAppliedRevision)
{
    TemporaryOptionsFile file;
    file.Write("unchanged");
    auto watchState = TGE::detail::CreateOptionsFileWatchState();

    std::mutex mutex;
    std::condition_variable observed;
    int calls = 0;
    auto subscription = TGE::detail::WatchOptionsFile(
        file.path,
        std::chrono::milliseconds(20),
        [&]
        {
            TGE::detail::RecordAppliedOptionsFile(
                watchState,
                true,
                "changed");
            {
                std::scoped_lock lock(mutex);
                ++calls;
            }
            observed.notify_all();
            return true;
        },
        watchState);

    // The provider's initial Apply occurs after Watch is installed.
    TGE::detail::RecordAppliedOptionsFile(
        watchState,
        true,
        "unchanged");

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    {
        std::scoped_lock lock(mutex);
        EXPECT_EQ(calls, 0);
    }

    file.Write("changed");
    std::unique_lock lock(mutex);
    EXPECT_TRUE(observed.wait_for(
        lock,
        std::chrono::seconds(2),
        [&] { return calls >= 1; }));
    EXPECT_EQ(calls, 1);
}

TEST(OptionsMonitorTests, FileWatcherReloadsMismatchedAppliedRevision)
{
    TemporaryOptionsFile file;
    file.Write("current");
    auto watchState = TGE::detail::CreateOptionsFileWatchState();

    std::mutex mutex;
    std::condition_variable observed;
    int calls = 0;
    auto subscription = TGE::detail::WatchOptionsFile(
        file.path,
        std::chrono::milliseconds(20),
        [&]
        {
            TGE::detail::RecordAppliedOptionsFile(
                watchState,
                true,
                "current");
            {
                std::scoped_lock lock(mutex);
                ++calls;
            }
            observed.notify_all();
            return true;
        },
        watchState);

    TGE::detail::RecordAppliedOptionsFile(
        watchState,
        true,
        "previous");

    std::unique_lock lock(mutex);
    EXPECT_TRUE(observed.wait_for(
        lock,
        std::chrono::seconds(2),
        [&] { return calls >= 1; }));
    EXPECT_EQ(calls, 1);
}

TEST(OptionsMonitorTests, FileWatcherRequiresInitialSignatureAcceptance)
{
    TemporaryOptionsFile file;
    file.Write("unchanged");

    std::mutex mutex;
    std::condition_variable observed;
    int calls = 0;
    auto subscription = TGE::detail::WatchOptionsFile(
        file.path,
        std::chrono::milliseconds(20),
        [&]
        {
            {
                std::scoped_lock lock(mutex);
                ++calls;
            }
            observed.notify_all();
            return true;
        });

    std::unique_lock lock(mutex);
    EXPECT_TRUE(observed.wait_for(
        lock,
        std::chrono::seconds(2),
        [&] { return calls >= 1; }));
}

TEST(OptionsMonitorTests, RequiredFileRecoveryRetriesTheRestoredRevision)
{
    TemporaryOptionsFile file;
    file.Write(R"({"count":5})");

    TGE::ServiceCollection services;
    auto builder = services.AddOptions<TestOptions>();
    builder.FromJsonFile(
        file.path,
        {
            .reloadOnChange = true,
            .pollingInterval = std::chrono::milliseconds(20)
        });

    auto monitor = builder.GetMonitor();
    std::this_thread::sleep_for(std::chrono::milliseconds(75));

    std::error_code removeError;
    ASSERT_TRUE(std::filesystem::remove(file.path, removeError));
    ASSERT_FALSE(removeError) << removeError.message();

    const auto failureDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!monitor->LastError() &&
           std::chrono::steady_clock::now() < failureDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(monitor->LastError());

    // Restore the same bytes as the last good revision. The watcher must retry
    // because the missing-file signature was rejected, even though the
    // restored signature equals the prior baseline.
    file.Write(R"({"count":5})");
    const auto recoveryDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (monitor->LastError() &&
           std::chrono::steady_clock::now() < recoveryDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_FALSE(monitor->LastError());
    EXPECT_EQ(monitor->Current()->count, 5);
}

TEST(OptionsIocTests, ExposesWritableJsonOnlyThroughExplicitRegistration)
{
    TemporaryOptionsFile file;
    auto json =
        std::make_shared<TGE::JsonFileOptionsProvider<TestOptions>>(
            file.path,
            TGE::JsonFileOptionsProviderSettings { .optional = true });

    TGE::ServiceCollection services;
    services.AddOptions<TestOptions>().AddProvider(json);
    EXPECT_FALSE(services.Contains<TGE::IOptionsStore<TestOptions>>());

    services.AddSingleton<TGE::IOptionsStore<TestOptions>>(json);
    EXPECT_TRUE(services.Contains<TGE::IOptionsStore<TestOptions>>());

    auto provider = services.BuildServiceProvider();
    auto resolved =
        provider->GetRequiredService<TGE::IOptionsStore<TestOptions>>();
    auto scope = provider->CreateScope();
    auto scoped =
        scope->GetRequiredService<TGE::IOptionsStore<TestOptions>>();
    const auto expectedStore =
        std::static_pointer_cast<TGE::IOptionsStore<TestOptions>>(json);

    EXPECT_EQ(resolved, expectedStore);
    EXPECT_EQ(scoped, resolved);

    TestOptions persisted;
    persisted.name = "explicit capability";
    ASSERT_TRUE(resolved->Save(persisted));
    auto roundTrip =
        TGE::DeserializeOptions<TestOptions>(file.Read());
    ASSERT_TRUE(roundTrip) << roundTrip.error().message;
    EXPECT_EQ(roundTrip->name, "explicit capability");
}

TEST(OptionsIocTests, ConvenienceSourcesDoNotGrantWriteAuthority)
{
    TemporaryOptionsFile file;
    TGE::ServiceCollection services;
    services.AddOptions<TestOptions>()
        .FromJsonFile(file.path, { .optional = true })
        .FromEnvironment("TGE_READ_ONLY_OPTIONS");

    EXPECT_FALSE(services.Contains<TGE::IOptionsStore<TestOptions>>());
}

TEST(OptionsIocTests, RegistersReadOnlyAndConcreteSingletonsAcrossScopes)
{
    TGE::ServiceCollection services;
    services.AddOptions<TestOptions>();
    services.AddOptions<TestOptions>().Configure(
        [](TestOptions& value)
        {
            value.name = "composed";
        });
    services.AddOptions<OtherOptions>().Configure(
        [](OtherOptions& value)
        {
            value.active = true;
        });

    auto provider = services.BuildServiceProvider();
    auto readOnly =
        provider->GetRequiredService<TGE::IOptionsMonitor<TestOptions>>();
    auto concrete =
        provider->GetRequiredService<TGE::OptionsMonitor<TestOptions>>();
    auto other =
        provider->GetRequiredService<TGE::IOptionsMonitor<OtherOptions>>();
    auto scope = provider->CreateScope();
    auto scoped =
        scope->GetRequiredService<TGE::IOptionsMonitor<TestOptions>>();

    EXPECT_EQ(readOnly->Current()->name, "composed");
    EXPECT_TRUE(other->Current()->active);
    EXPECT_EQ(readOnly.get(), scoped.get());
    EXPECT_EQ(
        static_cast<TGE::IOptionsMonitor<TestOptions>*>(concrete.get()),
        readOnly.get());
}

TEST(OptionsIocTests, RejectsManualMonitorCollisionsBeforeRegistration)
{
    TGE::ServiceCollection services;
    auto monitor = std::make_shared<TGE::OptionsMonitor<TestOptions>>();
    std::shared_ptr<TGE::IOptionsMonitor<TestOptions>> readOnly = monitor;
    services.AddSingleton(readOnly);

    EXPECT_THROW(
        services.AddOptions<TestOptions>(),
        std::logic_error);
    EXPECT_FALSE(services.Contains<TGE::OptionsMonitor<TestOptions>>());
}

TEST(OptionsMonitorTests, ConcurrentReadersNeverObserveTornValues)
{
    auto monitor =
        std::make_shared<TGE::OptionsMonitor<InvariantOptions>>();
    std::atomic<bool> running { true };
    std::atomic<int> violations {};

    std::vector<std::jthread> readers;
    for (int index = 0; index < 4; ++index)
    {
        readers.emplace_back(
            [&]
            {
                while (running.load(std::memory_order_acquire))
                {
                    const auto current = monitor->Current();
                    if (current->left != current->right)
                    {
                        violations.fetch_add(1);
                    }
                }
            });
    }

    for (std::uint64_t value = 1; value <= 2'000; ++value)
    {
        auto result = monitor->Set(InvariantOptions {
            .left = value,
            .right = value
        });
        ASSERT_TRUE(result);
    }

    running.store(false, std::memory_order_release);
    readers.clear();
    EXPECT_EQ(violations.load(), 0);
}
