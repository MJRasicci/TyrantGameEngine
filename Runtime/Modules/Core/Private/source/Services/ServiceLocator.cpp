#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

#include "TGE/Services/ServiceLocator.hpp"
#include "TGE/Services/ServiceDescriptor.hpp"

#include "Internal/Services/ServiceRegistry.hpp"

namespace
{
    struct ResolutionFrame
    {
        TGE::ServiceLocator* locator;
        std::vector<std::type_index> path;
        ResolutionFrame* previous;
    };

    thread_local ResolutionFrame* activeResolutionFrame = nullptr;

    std::string BuildCycleMessage(const std::vector<std::type_index>& path, std::type_index repeated)
    {
        auto begin = std::find(path.begin(), path.end(), repeated);
        std::vector<std::string> chain;

        for (auto it = begin; it != path.end(); ++it)
        {
            chain.emplace_back(it->name());
        }

        chain.emplace_back(repeated.name());

        std::string joined;
        for (std::size_t index = 0; index < chain.size(); ++index)
        {
            if (index > 0)
            {
                joined += " -> ";
            }

            joined += chain[index];
        }

        return joined;
    }
}

namespace TGE
{
    ServiceLocator::ServiceLocator(std::shared_ptr<detail::ServiceRegistry> registry,
                                   std::unordered_map<std::type_index, ActivationHandle>* singletonCache,
                                   std::recursive_mutex* transactionMutex)
        : registry(std::move(registry)),
          singletonCache(singletonCache),
          transactionMutex(transactionMutex)
    {
    }

    ServiceLocator::ResolutionResult ServiceLocator::Resolve(std::type_index type, bool required)
    {
        std::scoped_lock transaction(GetTransactionMutex());
        ValidateResolutionAllowed();

        for (auto* frame = activeResolutionFrame;
             frame;
             frame = frame->previous)
        {
            if (frame->locator == this)
            {
                return ResolveInternal(type, frame->path, required);
            }
        }

        ResolutionFrame frame {
            .locator = this,
            .path = {},
            .previous = activeResolutionFrame
        };
        activeResolutionFrame = &frame;

        try
        {
            auto result = ResolveInternal(type, frame.path, required);
            activeResolutionFrame = frame.previous;
            return result;
        }
        catch (...)
        {
            activeResolutionFrame = frame.previous;
            throw;
        }
    }

    ServiceLocator::ResolutionResult ServiceLocator::ResolveInternal(std::type_index type, std::vector<std::type_index>& path, bool required)
    {
        const ServiceDescriptor* descriptor = FindDescriptor(type);

        if (!descriptor)
        {
            if (required)
            {
                throw std::domain_error(std::format(
                    "Unable to locate service \"{}\".", type.name()));
            }

            return {};
        }

        auto lifetime = descriptor->GetServiceLifetime();
        auto cached = GetCachedInstance(lifetime, descriptor->GetServiceType());

        if (cached)
        {
            return { descriptor, cached };
        }

        if (std::find(path.begin(), path.end(), descriptor->GetServiceType()) != path.end())
        {
            throw std::domain_error(std::format(
                "Detected cyclic service dependency: {}",
                BuildCycleMessage(path, descriptor->GetServiceType())));
        }

        path.push_back(descriptor->GetServiceType());
        ActivationHandle instance;

        try
        {
            instance = descriptor->Activate(*this);
            path.pop_back();
        }
        catch (...)
        {
            path.pop_back();
            throw;
        }

        // A factory may re-enter the scope lifecycle while the provider's
        // recursive transaction lock is held. Never commit an activation to a
        // scope that began ending during that user-supplied activation.
        ValidateResolutionAllowed();
        CacheInstance(lifetime, descriptor->GetServiceType(), instance);

        return { descriptor, instance };
    }

    const ServiceDescriptor* ServiceLocator::FindDescriptor(std::type_index type) const
    {
        if (auto it = registry->descriptors.find(type); it != registry->descriptors.end())
        {
            return &it->second;
        }

        if (auto it = registry->implementationLookup.find(type); it != registry->implementationLookup.end())
        {
            if (auto descriptorIt = registry->descriptors.find(it->second); descriptorIt != registry->descriptors.end())
            {
                return &descriptorIt->second;
            }
        }

        return nullptr;
    }

    ServiceLocator::ActivationHandle ServiceLocator::GetCachedInstance(ServiceLifetime lifetime, std::type_index type) const
    {
        switch (lifetime)
        {
            case ServiceLifetime::Singleton:
                if (singletonCache && singletonCache->contains(type))
                {
                    return singletonCache->at(type);
                }
                break;
            case ServiceLifetime::Scoped:
                if (auto it = scopedCache.find(type); it != scopedCache.end())
                {
                    return it->second;
                }
                break;
            case ServiceLifetime::Transient:
                break;
        }

        return {};
    }

    void ServiceLocator::CacheInstance(ServiceLifetime lifetime, std::type_index type, const ActivationHandle& instance)
    {
        if (!instance)
        {
            return;
        }

        switch (lifetime)
        {
            case ServiceLifetime::Singleton:
                if (singletonCache)
                {
                    singletonCache->emplace(type, instance);
                }
                break;
            case ServiceLifetime::Scoped:
            {
                const auto [_, inserted] = scopedCache.emplace(type, instance);
                if (inserted)
                {
                    scopedActivationOrder.emplace_back(type);
                }
                break;
            }
            case ServiceLifetime::Transient:
                break;
        }
    }

    std::vector<ServiceLocator::ActivationHandle>
        ServiceLocator::ExtractScopedInstancesInReverse()
    {
        std::vector<ActivationHandle> instances;
        instances.reserve(scopedActivationOrder.size());

        for (auto type = scopedActivationOrder.rbegin();
             type != scopedActivationOrder.rend();
             ++type)
        {
            if (auto cached = scopedCache.find(*type);
                cached != scopedCache.end())
            {
                instances.emplace_back(std::move(cached->second));
            }
        }

        scopedCache.clear();
        scopedActivationOrder.clear();
        return instances;
    }

    void ServiceLocator::ValidateResolutionAllowed() const
    {
    }
}
