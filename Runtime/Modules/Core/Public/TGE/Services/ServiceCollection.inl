#pragma once

#include <utility>

#include "TGE/Options/OptionsBuilder.hpp"
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

    template<OptionsType TOptions>
    OptionsBuilder<TOptions> ServiceCollection::AddOptions(TOptions defaults)
    {
        const std::type_index optionsType = typeid(TOptions);
        if (const auto existing = optionsMonitors.find(optionsType);
            existing != optionsMonitors.end())
        {
            return OptionsBuilder<TOptions>(
                std::static_pointer_cast<OptionsMonitor<TOptions>>(
                    existing->second));
        }

        if (Contains<OptionsMonitor<TOptions>>() ||
            Contains<IOptionsMonitor<TOptions>>())
        {
            throw std::logic_error(std::format(
                "Cannot add options for {} because its monitor service was "
                "registered outside AddOptions.",
                optionsType.name()));
        }

        auto monitor = std::make_shared<OptionsMonitor<TOptions>>(
            std::move(defaults));
        AddSingleton(monitor);

        std::shared_ptr<IOptionsMonitor<TOptions>> readOnly = monitor;
        AddSingleton(readOnly);

        optionsMonitors.emplace(optionsType, monitor);
        return OptionsBuilder<TOptions>(std::move(monitor));
    }
}
