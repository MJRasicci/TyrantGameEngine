#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

#include "TGE/Application/IHostedService.hpp"
#include "TGE/Execution/Task.hpp"
#include "TGE/Services/ServiceProvider.hpp"

#include "Internal/Services/ServiceRegistry.hpp"

#if TGE_HAS_NATIVE_STD_EXECUTION
namespace TGEExecutionBackend = std::execution;
#else
namespace TGEExecutionBackend = stdexec;
#endif

namespace TGE
{
    struct ServiceScope::Impl
    {
        struct CompletionWaiter
        {
            void* operation = nullptr;
            void (*complete)(
                void*,
                const std::exception_ptr&) noexcept = nullptr;
        };

        struct CompletionSender
        {
            using sender_concept = TGEExecutionBackend::sender_tag;
            using completion_signatures =
                TGEExecutionBackend::completion_signatures<
                    TGEExecutionBackend::set_value_t(),
                    TGEExecutionBackend::set_error_t(std::exception_ptr)>;

            Impl* scope = nullptr;

            template<class TReceiver>
            struct Operation
            {
                using operation_state_concept =
                    TGEExecutionBackend::operation_state_tag;

                Operation(Impl* scope, TReceiver receiver)
                    : scope(scope),
                      receiver(std::move(receiver))
                {
                }

                Operation(const Operation&) = delete;
                Operation& operator=(const Operation&) = delete;
                Operation(Operation&&) = delete;
                Operation& operator=(Operation&&) = delete;

                ~Operation()
                {
                    std::scoped_lock lock(scope->completionMutex);
                    std::erase_if(
                        scope->completionWaiters,
                        [this](const CompletionWaiter& waiter)
                        {
                            return waiter.operation == this;
                        });
                }

                void start() noexcept
                {
                    std::exception_ptr failure;
                    bool completeImmediately = false;

                    {
                        std::scoped_lock lock(scope->completionMutex);

                        if (scope->completionFinished)
                        {
                            completeImmediately = true;
                            failure = scope->completionFailure;
                        }
                        else
                        {
                            try
                            {
                                scope->completionWaiters.emplace_back(
                                    CompletionWaiter {
                                        .operation = this,
                                        .complete = &Operation::Complete
                                    });
                            }
                            catch (...)
                            {
                                completeImmediately = true;
                                failure = std::current_exception();
                            }
                        }
                    }

                    if (completeImmediately)
                    {
                        Complete(this, failure);
                    }
                }

                static void Complete(
                    void* operation,
                    const std::exception_ptr& failure) noexcept
                {
                    auto* self = static_cast<Operation*>(operation);

                    if (failure)
                    {
                        TGEExecutionBackend::set_error(
                            std::move(self->receiver),
                            failure);
                    }
                    else
                    {
                        TGEExecutionBackend::set_value(
                            std::move(self->receiver));
                    }
                }

                Impl* scope;
                TReceiver receiver;
            };

            template<class TReceiver>
            auto connect(TReceiver receiver) const
            {
                return Operation<TReceiver> {
                    scope,
                    std::move(receiver)
                };
            }
        };

        std::atomic<ServiceScopeState> state { ServiceScopeState::Active };
        std::weak_ptr<ServiceScope> parent;
        std::vector<std::weak_ptr<ServiceScope>> children;
        std::vector<Cleanup> cleanups;

        std::mutex completionMutex;
        bool completionFinished = false;
        std::exception_ptr completionFailure;
        std::vector<CompletionWaiter> completionWaiters;
    };

    ServiceProvider::ServiceProvider(std::shared_ptr<detail::ServiceRegistry> registry)
        : ServiceLocator(
              std::move(registry),
              &singletonStorage,
              &transactionMutex)
    {
    }

    std::shared_ptr<ServiceScope> ServiceProvider::CreateScope()
    {
        return std::shared_ptr<ServiceScope>(
            new ServiceScope(shared_from_this(), {}));
    }

    std::vector<std::shared_ptr<IHostedService>> ServiceProvider::GetHostedServices()
    {
        std::vector<std::shared_ptr<IHostedService>> services;
        services.reserve(GetRegistry()->hostedServiceFactories.size());

        for (const auto& factory : GetRegistry()->hostedServiceFactories)
        {
            services.emplace_back(factory(*this));
        }

        return services;
    }

    ServiceScope::ServiceScope(
        std::shared_ptr<ServiceProvider> rootProvider,
        std::weak_ptr<ServiceScope> parent)
        : ServiceLocator(
              rootProvider->GetRegistry(),
              &rootProvider->singletonStorage,
              &rootProvider->transactionMutex),
          rootOwner(std::move(rootProvider)),
          impl(std::make_unique<Impl>())
    {
        impl->parent = std::move(parent);
    }

    ServiceScope::~ServiceScope() noexcept
    {
        if (GetState() == ServiceScopeState::Ended)
        {
            return;
        }

        try
        {
            auto result = Execution::SyncWait(EndCoreAsync());
            (void)result;
        }
        catch (...)
        {
            // Destructors cannot surface asynchronous cleanup failures. An
            // explicit EndAsync call remains the observable cleanup path.
        }
    }

    std::shared_ptr<ServiceScope> ServiceScope::CreateScope()
    {
        std::scoped_lock transaction(GetTransactionMutex());

        if (!IsResolutionAllowed())
        {
            throw std::logic_error(
                "Cannot create a child from a service scope that is ending or ended.");
        }

        auto child = std::shared_ptr<ServiceScope>(
            new ServiceScope(rootOwner, weak_from_this()));

        impl->children.erase(
            std::remove_if(
                impl->children.begin(),
                impl->children.end(),
                [](const auto& existing)
                {
                    return existing.expired();
                }),
            impl->children.end());
        impl->children.emplace_back(child);
        return child;
    }

    void ServiceScope::RegisterCleanup(Cleanup cleanup)
    {
        if (!cleanup)
        {
            throw std::invalid_argument(
                "A service scope cleanup callback cannot be empty.");
        }

        std::scoped_lock transaction(GetTransactionMutex());

        if (!IsResolutionAllowed())
        {
            throw std::logic_error(
                "Cannot register cleanup after a service scope begins ending.");
        }

        impl->cleanups.emplace_back(std::move(cleanup));
    }

    Task<void> ServiceScope::EndAsync()
    {
        return EndOwnedAsync(shared_from_this());
    }

    ServiceScopeState ServiceScope::GetState() const noexcept
    {
        return impl->state.load(std::memory_order_acquire);
    }

    Task<void> ServiceScope::EndOwnedAsync(
        std::shared_ptr<ServiceScope> scope)
    {
        co_await scope->EndCoreAsync();
    }

    Task<void> ServiceScope::EndCoreAsync()
    {
        std::vector<std::shared_ptr<ServiceScope>> children;
        std::vector<Cleanup> cleanups;
        bool ownsEnding = false;

        {
            std::scoped_lock transaction(GetTransactionMutex());

            if (impl->state.load(std::memory_order_relaxed) ==
                ServiceScopeState::Active)
            {
                children.reserve(impl->children.size());
                cleanups.reserve(impl->cleanups.size());

                for (auto child = impl->children.rbegin();
                     child != impl->children.rend();
                     ++child)
                {
                    if (auto liveChild = child->lock())
                    {
                        children.emplace_back(std::move(liveChild));
                    }
                }
                impl->children.clear();

                for (auto cleanup = impl->cleanups.rbegin();
                     cleanup != impl->cleanups.rend();
                     ++cleanup)
                {
                    cleanups.emplace_back(std::move(*cleanup));
                }
                impl->cleanups.clear();

                impl->state.store(
                    ServiceScopeState::Ending,
                    std::memory_order_release);
                ownsEnding = true;
            }
        }

        if (!ownsEnding)
        {
            co_await Impl::CompletionSender { impl.get() };
        }
        else
        {
            std::exception_ptr failure;

            for (const auto& child : children)
            {
                try
                {
                    co_await Execution::StoppedAsError(
                        child->EndAsync(),
                        std::make_exception_ptr(std::runtime_error(
                            "Child service scope cleanup completed through cancellation.")));
                }
                catch (...)
                {
                    if (!failure)
                    {
                        failure = std::current_exception();
                    }
                }
            }
            children.clear();

            for (auto& cleanup : cleanups)
            {
                try
                {
                    co_await Execution::StoppedAsError(
                        cleanup(),
                        std::make_exception_ptr(std::runtime_error(
                            "Service scope cleanup completed through cancellation.")));
                }
                catch (...)
                {
                    if (!failure)
                    {
                        failure = std::current_exception();
                    }
                }
            }
            cleanups.clear();

            std::vector<ActivationHandle> scopedInstances;
            try
            {
                std::scoped_lock transaction(GetTransactionMutex());
                scopedInstances = ExtractScopedInstancesInReverse();
            }
            catch (...)
            {
                if (!failure)
                {
                    failure = std::current_exception();
                }
            }

            for (auto& instance : scopedInstances)
            {
                instance.reset();
            }

            std::vector<Impl::CompletionWaiter> waiters;
            {
                std::scoped_lock completionLock(impl->completionMutex);
                impl->completionFailure = failure;
                impl->completionFinished = true;
                impl->state.store(
                    ServiceScopeState::Ended,
                    std::memory_order_release);
                waiters = std::move(impl->completionWaiters);
            }

            for (const auto& waiter : waiters)
            {
                waiter.complete(waiter.operation, failure);
            }

            if (failure)
            {
                std::rethrow_exception(failure);
            }
        }
    }

    void ServiceScope::ValidateResolutionAllowed() const
    {
        if (!IsResolutionAllowed())
        {
            throw std::logic_error(
                "Cannot resolve services from a scope that is ending or ended.");
        }
    }

    bool ServiceScope::IsResolutionAllowed() const noexcept
    {
        if (GetState() != ServiceScopeState::Active)
        {
            return false;
        }

        auto parent = impl->parent.lock();
        return !parent || parent->IsResolutionAllowed();
    }
}
