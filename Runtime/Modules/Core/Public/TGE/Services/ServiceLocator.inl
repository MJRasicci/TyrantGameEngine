#include "TGE/Services/ServiceDescriptor.hpp"

#pragma once

namespace TGE
{
    template<IService TService>
    std::shared_ptr<TService> ServiceLocator::GetRequiredService()
    {
        auto result = Resolve(typeid(TService), true);
        return result.descriptor->template CastToService<TService>(result.instance);
    }

    template<IService TService>
    std::shared_ptr<TService> ServiceLocator::TryGetService()
    {
        auto result = Resolve(typeid(TService), false);

        if (!result.descriptor)
        {
            return nullptr;
        }

        return result.descriptor->template CastToService<TService>(result.instance);
    }
}

