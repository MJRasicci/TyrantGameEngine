/**
 * @file InputDevice.hpp
 * @brief Platform-neutral input-device descriptions.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "TGE/Input/InputDeviceId.hpp"

namespace TGE
{
    /**
     * @brief Broad semantic category of an input device.
     */
    enum class InputDeviceKind
    {
        Unknown,
        Keyboard,
        Pointer,
        Touch,
        Pen,
        Gamepad
    };

    /**
     * @brief Stable descriptive snapshot of one connected input device.
     *
     * Vendor and product identifiers are descriptive USB-style values when
     * available; their absence is not an error and they are not native handles.
     */
    struct InputDeviceDescriptor
    {
        InputDeviceId id;
        InputDeviceKind kind { InputDeviceKind::Unknown };
        std::string name;
        std::optional<std::uint16_t> vendorId;
        std::optional<std::uint16_t> productId;
        bool virtualDevice { false };

        bool operator==(const InputDeviceDescriptor&) const = default;
    };
}
