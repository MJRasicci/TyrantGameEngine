/**
 * @file ServiceProvider.hpp
 * @brief Defines the root provider and child scopes for service resolution.
 */

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "TGE/Execution/Task.hpp"
#include "TGE/Export.hpp"
#include "TGE/Services/ServiceLocator.hpp"
#include "TGE/Services/ServiceScopeState.hpp"

namespace TGE
{
    class Application;
    class IHostedService;

    /**
     * @class ServiceProvider
     * @brief Root service locator that owns singleton instances.
     */
    class TGE_API ServiceProvider : public ServiceLocator, public std::enable_shared_from_this<ServiceProvider>
    {
    public:
        /**
         * @brief Create a new scope that inherits singleton registrations.
         */
        std::shared_ptr<ServiceScope> CreateScope();

    protected:
        using ActivationHandle = ServiceLocator::ActivationHandle;

        explicit ServiceProvider(std::shared_ptr<detail::ServiceRegistry> registry);

    private:
        std::vector<std::shared_ptr<IHostedService>> GetHostedServices();

        std::unordered_map<std::type_index, ActivationHandle> singletonStorage;
        std::recursive_mutex transactionMutex;

        friend class Application;
        friend class ServiceCollection;
        friend class ServiceScope;
    };

    /**
     * @class ServiceScope
     * @brief Provides scoped lifetime semantics for services.
     */
    class TGE_API ServiceScope
        : public ServiceLocator,
          public std::enable_shared_from_this<ServiceScope>
    {
    public:
        using Cleanup = std::move_only_function<Task<void>()>;

        /**
         * @brief End this scope when it is destroyed if explicit ending was omitted.
         */
        ~ServiceScope() noexcept override;

        /**
         * @brief Create a child scope whose lifetime is bounded by this scope.
         *
         * Child scopes retain independent scoped-service caches. Ending a
         * parent ends its live children in reverse creation order.
         */
        std::shared_ptr<ServiceScope> CreateScope();

        /**
         * @brief Register asynchronous cleanup performed before cached services are released.
         *
         * Cleanups execute in reverse registration order. Registration is
         * rejected after ending begins.
         */
        void RegisterCleanup(Cleanup cleanup);

        /**
         * @brief End this scope exactly once and share completion with concurrent callers.
         *
         * Live children, registered cleanups, and cached scoped instances are
         * released in that order, with each group processed in reverse order.
         */
        Task<void> EndAsync();

        /**
         * @brief Return the current lifecycle state.
         */
        ServiceScopeState GetState() const noexcept;

    private:
        using ActivationHandle = ServiceLocator::ActivationHandle;

        struct Impl;

        ServiceScope(
            std::shared_ptr<ServiceProvider> rootProvider,
            std::weak_ptr<ServiceScope> parent);

        static Task<void> EndOwnedAsync(std::shared_ptr<ServiceScope> scope);
        Task<void> EndCoreAsync();
        void ValidateResolutionAllowed() const override;
        bool IsResolutionAllowed() const noexcept;

        std::shared_ptr<ServiceProvider> rootOwner;
        std::unique_ptr<Impl> impl;

        friend class ServiceProvider;
    };
}
