#pragma once

#include "Export.hpp"
#include "Services/ServiceDescriptor.hpp"
#include "Services/ServiceCollection.hpp"
#include "Services/ServiceLocator.hpp"

namespace TGE
{
    class ServiceScope;

    class TGE_API ServiceProvider : public ServiceLocator
    {
    public:
        std::shared_ptr<ServiceScope> CreateScope();

    protected:
        ServiceProvider(ServiceCollection services);

    private:
        friend ServiceCollection;
    };

    class TGE_API ServiceScope : public ServiceLocator
    {
    protected:
        ServiceScope(ServiceLocator& provider);

    private:
        friend ServiceProvider;
    };
}
