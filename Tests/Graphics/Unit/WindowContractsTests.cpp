#include <type_traits>

#include <gtest/gtest.h>

#include "TGE/Graphics.hpp"
#include "TGE/Services/Service.hpp"

namespace
{
    static_assert(std::is_abstract_v<TGE::IWindow>);
    static_assert(std::is_abstract_v<TGE::IWindowManager>);
    static_assert(TGE::IService<TGE::IWindowManager>);

    TEST(WindowContracts, DefaultDescriptorRepresentsVisibleTopLevelWindow)
    {
        const TGE::WindowDescriptor descriptor;

        EXPECT_EQ(descriptor.role, TGE::WindowRole::TopLevel);
        EXPECT_FALSE(descriptor.parent.has_value());
        EXPECT_TRUE(descriptor.initiallyVisible);
        EXPECT_TRUE(descriptor.chrome.decorations);
        EXPECT_TRUE(descriptor.chrome.resizable);
    }

    TEST(WindowContracts, DefaultWindowIdentifierIsInvalid)
    {
        const TGE::WindowId invalid;
        const auto valid = TGE::WindowId::FromValue(42);

        EXPECT_FALSE(static_cast<bool>(invalid));
        EXPECT_TRUE(static_cast<bool>(valid));
        EXPECT_EQ(valid.Value(), 42);
    }
}
