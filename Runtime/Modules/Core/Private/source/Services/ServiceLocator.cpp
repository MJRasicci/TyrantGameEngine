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
                                   ServiceLocator* root,
                                   ServiceLocator* parent)
        : registry(std::move(registry)),
          singletonCache(singletonCache),
          rootLocator(root),
          parentLocator(parent)
    {
    }

    ServiceLocator::ResolutionResult ServiceLocator::Resolve(std::type_index type, bool required)
    {
        std::vector<std::type_index> localPath;
        auto* previousPath = activePath;

        if (previousPath == nullptr)
        {
            activePath = &localPath;
        }

        auto result = ResolveInternal(type, *activePath, required);

        if (previousPath == nullptr)
        {
            activePath = nullptr;
        }

        return result;
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
        auto instance = descriptor->Activate(*this);
        path.pop_back();

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
                scopedCache.emplace(type, instance);
                break;
            case ServiceLifetime::Transient:
                break;
        }
    }
}

