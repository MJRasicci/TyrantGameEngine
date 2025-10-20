#pragma once

#include <utility>

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
}

