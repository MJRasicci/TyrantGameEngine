#pragma once

#include <utility>

#include "TGE/Services/ServiceLocator.hpp"

namespace TGE
{
    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    void ServiceCollection::AddSingleton()
    {
        Register(ServiceDescriptor::Singleton<TService, TImplementation>());
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    void ServiceCollection::AddScoped()
    {
        Register(ServiceDescriptor::Scoped<TService, TImplementation>());
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    void ServiceCollection::AddTransient()
    {
        Register(ServiceDescriptor::Transient<TService, TImplementation>());
    }

    template<class TService>
        requires IService<TService>
    void ServiceCollection::AddSingleton(const std::shared_ptr<TService>& instance)
    {
        Register(ServiceDescriptor::Singleton(instance));
    }

    template<class TService>
        requires IService<TService>
    void ServiceCollection::AddScoped(const std::shared_ptr<TService>& instance)
    {
        Register(ServiceDescriptor::Scoped(instance));
    }

    template<class TService>
        requires IService<TService>
    void ServiceCollection::AddTransient(const std::shared_ptr<TService>& instance)
    {
        Register(ServiceDescriptor::Transient(instance));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    void ServiceCollection::AddSingleton(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        Register(ServiceDescriptor::Singleton<TService, TImplementation>(std::move(factory)));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    void ServiceCollection::AddScoped(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        Register(ServiceDescriptor::Scoped<TService, TImplementation>(std::move(factory)));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    void ServiceCollection::AddTransient(std::function<std::shared_ptr<TService>(ServiceLocator&)> factory)
    {
        Register(ServiceDescriptor::Transient<TService, TImplementation>(std::move(factory)));
    }

    template<class TService>
        requires IService<TService> &&
                 std::derived_from<TService, IHostedService> &&
                 (!std::is_abstract_v<TService>)
    void ServiceCollection::AddHostedService()
    {
        RegisterHostedService(
            ServiceDescriptor::Singleton<TService>(),
            [](ServiceLocator& locator) -> std::shared_ptr<IHostedService>
            {
                return locator.template GetRequiredService<TService>();
            });
    }

    template<class TService>
        requires IService<TService>
    bool ServiceCollection::Contains() const noexcept
    {
        return Contains(typeid(TService));
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    bool ServiceCollection::TryAddSingleton()
    {
        if (Contains<TService>())
        {
            return false;
        }

        AddSingleton<TService, TImplementation>();
        return true;
    }

    template<class TService>
        requires IService<TService>
    bool ServiceCollection::TryAddSingleton(const std::shared_ptr<TService>& instance)
    {
        if (Contains<TService>())
        {
            return false;
        }

        AddSingleton(instance);
        return true;
    }

    template<class TService, class TImplementation>
        requires IService<TService> && IServiceImplementation<TService, TImplementation>
    bool ServiceCollection::TryAddTransient()
    {
        if (Contains<TService>())
        {
            return false;
        }

        AddTransient<TService, TImplementation>();
        return true;
    }
}
