#include "Internal/VulkanPresentation/VulkanWindowPresenterFactory.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

namespace TGE::Internal
{
    namespace
    {
        constexpr std::size_t FramesInFlight = 2;

        WindowError PresentationFailure(std::string message)
        {
            return WindowError {
                .code = WindowErrorCode::PlatformFailure,
                .message = std::move(message)
            };
        }

        WindowError VulkanFailure(
            std::string operation,
            VkResult result)
        {
            return PresentationFailure(std::format(
                "{} failed with Vulkan result {}.",
                operation,
                static_cast<std::int32_t>(result)));
        }

        template<class T>
        std::expected<std::vector<T>, WindowError> VulkanVector(
            std::string operation,
            auto&& enumerate)
        {
            for (;;)
            {
                std::uint32_t count = 0;
                auto result = enumerate(&count, nullptr);
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        std::move(operation),
                        result));
                }

                std::vector<T> values(count);
                if (count == 0)
                {
                    return values;
                }

                result = enumerate(&count, values.data());
                if (result == VK_INCOMPLETE)
                {
                    continue;
                }
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        std::move(operation),
                        result));
                }
                values.resize(count);
                return values;
            }
        }

        bool HasExtension(
            const std::vector<VkExtensionProperties>& extensions,
            const char* name)
        {
            return std::ranges::any_of(
                extensions,
                [name](const VkExtensionProperties& extension)
                {
                    return std::string_view(extension.extensionName) == name;
                });
        }

        struct QueueFamilies
        {
            std::uint32_t graphics { 0 };
            std::uint32_t present { 0 };
        };

        struct FrameResources
        {
            VkCommandBuffer commandBuffer { VK_NULL_HANDLE };
            VkSemaphore imageAvailable { VK_NULL_HANDLE };
            VkFence complete { VK_NULL_HANDLE };
        };

        struct PendingSwapchainResources
        {
            PendingSwapchainResources() = default;

            ~PendingSwapchainResources()
            {
                if (device == VK_NULL_HANDLE)
                {
                    return;
                }

                for (const auto fence : presentationComplete)
                {
                    if (fence != VK_NULL_HANDLE)
                    {
                        vkDestroyFence(device, fence, nullptr);
                    }
                }
                for (const auto semaphore : renderFinished)
                {
                    if (semaphore != VK_NULL_HANDLE)
                    {
                        vkDestroySemaphore(device, semaphore, nullptr);
                    }
                }
                if (swapchain != VK_NULL_HANDLE)
                {
                    vkDestroySwapchainKHR(device, swapchain, nullptr);
                }
            }

            PendingSwapchainResources(
                const PendingSwapchainResources&) = delete;
            PendingSwapchainResources& operator=(
                const PendingSwapchainResources&) = delete;

            VkDevice device { VK_NULL_HANDLE };
            VkSwapchainKHR swapchain { VK_NULL_HANDLE };
            VkFormat format { VK_FORMAT_UNDEFINED };
            VkExtent2D extent {};
            std::vector<VkImage> images;
            std::vector<VkSemaphore> renderFinished;
            std::vector<VkFence> presentationComplete;
            std::vector<bool> presentationPending;
            std::vector<bool> initialized;
            std::vector<VkFence> imagesInFlight;
        };

        struct PresentedWindow
        {
            WaylandWindowPresentationTarget target;
            VkSurfaceKHR surface { VK_NULL_HANDLE };
            VkSwapchainKHR swapchain { VK_NULL_HANDLE };
            VkFormat format { VK_FORMAT_UNDEFINED };
            VkExtent2D extent {};
            std::vector<VkImage> images;
            std::vector<VkSemaphore> renderFinished;
            std::vector<VkFence> presentationComplete;
            std::vector<bool> presentationPending;
            std::vector<bool> initialized;
            std::vector<VkFence> imagesInFlight;
            std::array<FrameResources, FramesInFlight> frames;
            std::size_t frameIndex { 0 };
            bool failed { false };
        };

        struct DeviceCandidate
        {
            VkPhysicalDevice device { VK_NULL_HANDLE };
            QueueFamilies queues;
            std::uint64_t score { 0 };
        };

        class VulkanWindowPresenter final : public IWindowPresenter
        {
        public:
            ~VulkanWindowPresenter() override
            {
                Shutdown();
            }

            WindowPresentationResult AttachWindow(
                WindowId id,
                const WindowPresentationTarget& target,
                FramebufferSize framebufferSize) override
            {
                if (stopped)
                {
                    return std::unexpected(WindowError {
                        .code = WindowErrorCode::InvalidState,
                        .message =
                            "The Vulkan window presenter has stopped."
                    });
                }
                if (!id ||
                    windows.contains(id) ||
                    noPresentationWindows.contains(id))
                {
                    return std::unexpected(WindowError {
                        .code = WindowErrorCode::InvalidState,
                        .message =
                            "The window already has a presentation attachment."
                    });
                }

                if (std::holds_alternative<std::monostate>(target))
                {
                    noPresentationWindows.emplace(id);
                    return {};
                }

                const auto* wayland =
                    std::get_if<WaylandWindowPresentationTarget>(&target);
                if (wayland == nullptr ||
                    wayland->display == nullptr ||
                    wayland->surface == nullptr)
                {
                    return std::unexpected(WindowError {
                        .code = WindowErrorCode::InvalidDescriptor,
                        .message =
                            "A Wayland presentation target requires a display "
                            "and surface."
                    });
                }

                PresentedWindow presentation {};
                bool presentationPending = false;
                try
                {
                    auto initialized = EnsureInstance();
                    if (!initialized)
                    {
                        return initialized;
                    }

                    presentation.target = *wayland;
                    presentationPending = true;
                    auto surface = CreateSurface(presentation);
                    if (!surface)
                    {
                        return surface;
                    }

                    auto deviceReady = EnsureDevice(presentation.surface);
                    if (!deviceReady)
                    {
                        DestroyPresentedWindow(presentation);
                        return deviceReady;
                    }

                    auto supported =
                        ValidatePresentationSupport(presentation.surface);
                    if (!supported)
                    {
                        DestroyPresentedWindow(presentation);
                        return supported;
                    }

                    auto frames = CreateFrameResources(presentation);
                    if (!frames)
                    {
                        DestroyPresentedWindow(presentation);
                        return frames;
                    }

                    auto [inserted, didInsert] =
                        windows.try_emplace(id);
                    if (!didInsert)
                    {
                        DestroyPresentedWindow(presentation);
                        presentationPending = false;
                        return std::unexpected(WindowError {
                            .code = WindowErrorCode::InvalidState,
                            .message =
                                "The Vulkan presenter rejected the window "
                            "attachment."
                        });
                    }
                    inserted->second = std::move(presentation);
                    presentationPending = false;

                    WindowPresentationResult prepared {};
                    if (framebufferSize.width > 0 &&
                        framebufferSize.height > 0)
                    {
                        prepared = RecreateSwapchain(
                            inserted->second,
                            framebufferSize);
                    }
                    if (!prepared)
                    {
                        DestroyPresentedWindow(inserted->second);
                        windows.erase(inserted);
                        return prepared;
                    }
                    return {};
                }
                catch (const std::exception& exception)
                {
                    if (presentationPending)
                    {
                        DestroyPresentedWindow(presentation);
                    }
                    DetachWindow(id);
                    return std::unexpected(PresentationFailure(std::format(
                        "Attaching a Vulkan window failed: {}",
                        exception.what())));
                }
                catch (...)
                {
                    if (presentationPending)
                    {
                        DestroyPresentedWindow(presentation);
                    }
                    DetachWindow(id);
                    return std::unexpected(PresentationFailure(
                        "Attaching a Vulkan window failed with an unknown "
                        "error."));
                }
            }

            WindowPresentationResult RedrawWindow(
                WindowId id,
                FramebufferSize framebufferSize) override
            {
                if (stopped)
                {
                    return std::unexpected(WindowError {
                        .code = WindowErrorCode::InvalidState,
                        .message =
                            "The Vulkan window presenter has stopped."
                    });
                }
                if (noPresentationWindows.contains(id))
                {
                    return {};
                }

                PresentedWindow* presentation = nullptr;
                try
                {
                    auto found = windows.find(id);
                    if (found == windows.end())
                    {
                        return std::unexpected(WindowError {
                            .code = WindowErrorCode::InvalidState,
                            .message =
                                "The window has no Vulkan presentation "
                                "attachment."
                        });
                    }

                    presentation = &found->second;
                    return RedrawPresentedWindow(
                        *presentation,
                        framebufferSize);
                }
                catch (const std::exception& exception)
                {
                    if (presentation != nullptr)
                    {
                        presentation->failed = true;
                    }
                    return std::unexpected(PresentationFailure(std::format(
                        "Redrawing a Vulkan window failed: {}",
                        exception.what())));
                }
                catch (...)
                {
                    if (presentation != nullptr)
                    {
                        presentation->failed = true;
                    }
                    return std::unexpected(PresentationFailure(
                        "Redrawing a Vulkan window failed with an unknown "
                        "error."));
                }
            }

            void DetachWindow(WindowId id) noexcept override
            {
                noPresentationWindows.erase(id);
                const auto found = windows.find(id);
                if (found == windows.end())
                {
                    return;
                }

                DestroyPresentedWindow(found->second);
                windows.erase(found);
            }

            void Shutdown() noexcept override
            {
                if (std::exchange(stopped, true))
                {
                    return;
                }

                if (device != VK_NULL_HANDLE)
                {
                    (void)vkDeviceWaitIdle(device);
                }

                for (auto& [id, presentation] : windows)
                {
                    (void)id;
                    DestroyPresentedWindow(presentation, false);
                }
                windows.clear();
                noPresentationWindows.clear();

                if (device != VK_NULL_HANDLE)
                {
                    if (commandPool != VK_NULL_HANDLE)
                    {
                        vkDestroyCommandPool(
                            device,
                            commandPool,
                            nullptr);
                        commandPool = VK_NULL_HANDLE;
                    }
                    vkDestroyDevice(device, nullptr);
                    device = VK_NULL_HANDLE;
                    graphicsQueue = VK_NULL_HANDLE;
                    presentQueue = VK_NULL_HANDLE;
                    physicalDevice = VK_NULL_HANDLE;
                }

                if (instance != VK_NULL_HANDLE)
                {
                    vkDestroyInstance(instance, nullptr);
                    instance = VK_NULL_HANDLE;
                }
            }

        private:
            WindowPresentationResult EnsureInstance()
            {
                if (instance != VK_NULL_HANDLE)
                {
                    return {};
                }

                auto extensions =
                    VulkanVector<VkExtensionProperties>(
                        "Enumerating Vulkan instance extensions",
                        [](std::uint32_t* count, VkExtensionProperties* values)
                        {
                            return vkEnumerateInstanceExtensionProperties(
                                nullptr,
                                count,
                                values);
                        });
                if (!extensions)
                {
                    return std::unexpected(std::move(extensions.error()));
                }
                if (!HasExtension(*extensions, VK_KHR_SURFACE_EXTENSION_NAME) ||
                    !HasExtension(
                        *extensions,
                        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) ||
                    !HasExtension(
                        *extensions,
                        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) ||
                    !HasExtension(
                        *extensions,
                        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
                {
                    return std::unexpected(PresentationFailure(
                        "The Vulkan loader does not expose the required "
                        "Wayland surface and presentation-maintenance "
                        "extensions."));
                }

                const std::array requiredExtensions {
                    VK_KHR_SURFACE_EXTENSION_NAME,
                    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
                    VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
                    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME
                };
                const VkApplicationInfo applicationInfo {
                    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                    .pNext = nullptr,
                    .pApplicationName = "Tyrant Game Engine",
                    .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
                    .pEngineName = "Tyrant Game Engine",
                    .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
                    .apiVersion = VK_API_VERSION_1_1
                };
                const VkInstanceCreateInfo createInfo {
                    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .pApplicationInfo = &applicationInfo,
                    .enabledLayerCount = 0,
                    .ppEnabledLayerNames = nullptr,
                    .enabledExtensionCount =
                        static_cast<std::uint32_t>(
                            requiredExtensions.size()),
                    .ppEnabledExtensionNames = requiredExtensions.data()
                };

                const auto result =
                    vkCreateInstance(&createInfo, nullptr, &instance);
                if (result != VK_SUCCESS)
                {
                    instance = VK_NULL_HANDLE;
                    return std::unexpected(VulkanFailure(
                        "Creating the Vulkan instance",
                        result));
                }
                return {};
            }

            WindowPresentationResult CreateSurface(
                PresentedWindow& presentation)
            {
                const VkWaylandSurfaceCreateInfoKHR createInfo {
                    .sType =
                        VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .flags = 0,
                    .display = static_cast<wl_display*>(
                        presentation.target.display),
                    .surface = static_cast<wl_surface*>(
                        presentation.target.surface)
                };
                const auto result = vkCreateWaylandSurfaceKHR(
                    instance,
                    &createInfo,
                    nullptr,
                    &presentation.surface);
                if (result != VK_SUCCESS)
                {
                    presentation.surface = VK_NULL_HANDLE;
                    return std::unexpected(VulkanFailure(
                        "Creating a Vulkan Wayland surface",
                        result));
                }
                return {};
            }

            WindowPresentationResult EnsureDevice(VkSurfaceKHR surface)
            {
                if (device != VK_NULL_HANDLE)
                {
                    return {};
                }

                auto devices = VulkanVector<VkPhysicalDevice>(
                    "Enumerating Vulkan physical devices",
                    [this](
                        std::uint32_t* count,
                        VkPhysicalDevice* values)
                    {
                        return vkEnumeratePhysicalDevices(
                            instance,
                            count,
                            values);
                    });
                if (!devices)
                {
                    return std::unexpected(std::move(devices.error()));
                }

                std::optional<DeviceCandidate> selected;
                for (const auto candidate : *devices)
                {
                    auto evaluated = EvaluateDevice(candidate, surface);
                    if (evaluated &&
                        (!selected ||
                         evaluated->score > selected->score))
                    {
                        selected = *evaluated;
                    }
                }
                if (!selected)
                {
                    return std::unexpected(PresentationFailure(
                        "No Vulkan physical device can render and present to "
                        "the Wayland surface."));
                }

                physicalDevice = selected->device;
                queueFamilies = selected->queues;

                const float priority = 1.0F;
                std::array<std::uint32_t, 2> familyIndices {
                    queueFamilies.graphics,
                    queueFamilies.present
                };
                const auto uniqueEnd = std::unique(
                    familyIndices.begin(),
                    familyIndices.end());

                std::array<VkDeviceQueueCreateInfo, 2> queueInfos {};
                std::uint32_t queueCount = 0;
                for (auto current = familyIndices.begin();
                     current != uniqueEnd;
                     ++current)
                {
                    queueInfos[queueCount++] = VkDeviceQueueCreateInfo {
                        .sType =
                            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .queueFamilyIndex = *current,
                        .queueCount = 1,
                        .pQueuePriorities = &priority
                    };
                }

                const std::array requiredExtensions {
                    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                    VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
                };
                const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT
                    maintenanceFeatures {
                        .sType =
                            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
                        .pNext = nullptr,
                        .swapchainMaintenance1 = VK_TRUE
                };
                const VkDeviceCreateInfo createInfo {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                    .pNext = &maintenanceFeatures,
                    .flags = 0,
                    .queueCreateInfoCount = queueCount,
                    .pQueueCreateInfos = queueInfos.data(),
                    .enabledLayerCount = 0,
                    .ppEnabledLayerNames = nullptr,
                    .enabledExtensionCount =
                        static_cast<std::uint32_t>(
                            requiredExtensions.size()),
                    .ppEnabledExtensionNames = requiredExtensions.data(),
                    .pEnabledFeatures = nullptr
                };
                auto result = vkCreateDevice(
                    physicalDevice,
                    &createInfo,
                    nullptr,
                    &device);
                if (result != VK_SUCCESS)
                {
                    device = VK_NULL_HANDLE;
                    physicalDevice = VK_NULL_HANDLE;
                    return std::unexpected(VulkanFailure(
                        "Creating the Vulkan logical device",
                        result));
                }

                vkGetDeviceQueue(
                    device,
                    queueFamilies.graphics,
                    0,
                    &graphicsQueue);
                vkGetDeviceQueue(
                    device,
                    queueFamilies.present,
                    0,
                    &presentQueue);

                const VkCommandPoolCreateInfo poolInfo {
                    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                    .pNext = nullptr,
                    .flags =
                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                    .queueFamilyIndex = queueFamilies.graphics
                };
                result = vkCreateCommandPool(
                    device,
                    &poolInfo,
                    nullptr,
                    &commandPool);
                if (result != VK_SUCCESS)
                {
                    vkDestroyDevice(device, nullptr);
                    device = VK_NULL_HANDLE;
                    physicalDevice = VK_NULL_HANDLE;
                    graphicsQueue = VK_NULL_HANDLE;
                    presentQueue = VK_NULL_HANDLE;
                    commandPool = VK_NULL_HANDLE;
                    return std::unexpected(VulkanFailure(
                        "Creating the Vulkan presentation command pool",
                        result));
                }
                return {};
            }

            std::optional<DeviceCandidate> EvaluateDevice(
                VkPhysicalDevice candidate,
                VkSurfaceKHR surface) const
            {
                auto extensions =
                    VulkanVector<VkExtensionProperties>(
                        "Enumerating Vulkan device extensions",
                        [candidate](
                            std::uint32_t* count,
                            VkExtensionProperties* values)
                        {
                            return vkEnumerateDeviceExtensionProperties(
                                candidate,
                                nullptr,
                                count,
                                values);
                        });
                if (!extensions ||
                    !HasExtension(
                        *extensions,
                        VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
                    !HasExtension(
                        *extensions,
                        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))
                {
                    return std::nullopt;
                }

                VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT
                    maintenanceFeatures {
                        .sType =
                            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
                        .pNext = nullptr,
                        .swapchainMaintenance1 = VK_FALSE
                    };
                VkPhysicalDeviceFeatures2 features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    .pNext = &maintenanceFeatures,
                    .features = {}
                };
                vkGetPhysicalDeviceFeatures2(candidate, &features);
                if (maintenanceFeatures.swapchainMaintenance1 != VK_TRUE)
                {
                    return std::nullopt;
                }

                std::uint32_t familyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(
                    candidate,
                    &familyCount,
                    nullptr);
                std::vector<VkQueueFamilyProperties> families(familyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(
                    candidate,
                    &familyCount,
                    families.data());

                std::optional<std::uint32_t> graphics;
                std::optional<std::uint32_t> present;
                for (std::uint32_t index = 0;
                     index < familyCount;
                     ++index)
                {
                    VkBool32 supportsPresentation = VK_FALSE;
                    if (vkGetPhysicalDeviceSurfaceSupportKHR(
                            candidate,
                            index,
                            surface,
                            &supportsPresentation) != VK_SUCCESS)
                    {
                        continue;
                    }

                    const bool supportsGraphics =
                        (families[index].queueFlags &
                         VK_QUEUE_GRAPHICS_BIT) != 0;
                    if (supportsGraphics && supportsPresentation)
                    {
                        graphics = index;
                        present = index;
                        break;
                    }
                    if (supportsGraphics && !graphics)
                    {
                        graphics = index;
                    }
                    if (supportsPresentation && !present)
                    {
                        present = index;
                    }
                }
                if (!graphics || !present)
                {
                    return std::nullopt;
                }

                auto formats = SurfaceFormats(candidate, surface);
                auto modes = PresentModes(candidate, surface);
                if (!formats || formats->empty() ||
                    !modes || modes->empty())
                {
                    return std::nullopt;
                }

                VkPhysicalDeviceProperties properties {};
                vkGetPhysicalDeviceProperties(candidate, &properties);
                std::uint64_t score =
                    properties.limits.maxImageDimension2D;
                switch (properties.deviceType)
                {
                    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                        score += 4'000'000;
                        break;
                    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                        score += 3'000'000;
                        break;
                    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                        score += 2'000'000;
                        break;
                    case VK_PHYSICAL_DEVICE_TYPE_CPU:
                        score += 1'000'000;
                        break;
                    default:
                        break;
                }

                return DeviceCandidate {
                    .device = candidate,
                    .queues = QueueFamilies {
                        .graphics = *graphics,
                        .present = *present
                    },
                    .score = score
                };
            }

            WindowPresentationResult ValidatePresentationSupport(
                VkSurfaceKHR surface) const
            {
                VkBool32 supported = VK_FALSE;
                const auto result =
                    vkGetPhysicalDeviceSurfaceSupportKHR(
                        physicalDevice,
                        queueFamilies.present,
                        surface,
                        &supported);
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        "Querying Vulkan surface presentation support",
                        result));
                }
                if (!supported)
                {
                    return std::unexpected(PresentationFailure(
                        "The selected Vulkan queue cannot present to this "
                        "Wayland surface."));
                }
                return {};
            }

            WindowPresentationResult CreateFrameResources(
                PresentedWindow& presentation)
            {
                std::array<VkCommandBuffer, FramesInFlight> buffers {};
                const VkCommandBufferAllocateInfo allocationInfo {
                    .sType =
                        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .commandPool = commandPool,
                    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    .commandBufferCount =
                        static_cast<std::uint32_t>(buffers.size())
                };
                auto result = vkAllocateCommandBuffers(
                    device,
                    &allocationInfo,
                    buffers.data());
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        "Allocating Vulkan presentation command buffers",
                        result));
                }
                for (std::size_t index = 0;
                     index < presentation.frames.size();
                     ++index)
                {
                    presentation.frames[index].commandBuffer =
                        buffers[index];
                }

                const VkSemaphoreCreateInfo semaphoreInfo {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0
                };
                const VkFenceCreateInfo fenceInfo {
                    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = VK_FENCE_CREATE_SIGNALED_BIT
                };
                for (std::size_t index = 0;
                     index < presentation.frames.size();
                     ++index)
                {
                    auto& frame = presentation.frames[index];
                    result = vkCreateSemaphore(
                        device,
                        &semaphoreInfo,
                        nullptr,
                        &frame.imageAvailable);
                    if (result == VK_SUCCESS)
                    {
                        result = vkCreateFence(
                            device,
                            &fenceInfo,
                            nullptr,
                            &frame.complete);
                    }
                    if (result != VK_SUCCESS)
                    {
                        return std::unexpected(VulkanFailure(
                            "Creating Vulkan presentation synchronization",
                            result));
                    }
                }
                return {};
            }

            WindowPresentationResult RedrawPresentedWindow(
                PresentedWindow& presentation,
                FramebufferSize framebufferSize)
            {
                if (presentation.failed)
                {
                    return std::unexpected(PresentationFailure(
                        "The Vulkan presentation attachment is no longer "
                        "usable."));
                }
                if (framebufferSize.width == 0 ||
                    framebufferSize.height == 0)
                {
                    DestroySwapchain(presentation);
                    return {};
                }

                for (int attempt = 0; attempt < 2; ++attempt)
                {
                    if (presentation.swapchain == VK_NULL_HANDLE ||
                        presentation.extent.width != framebufferSize.width ||
                        presentation.extent.height != framebufferSize.height)
                    {
                        auto recreated =
                            RecreateSwapchain(
                                presentation,
                                framebufferSize);
                        if (!recreated)
                        {
                            return recreated;
                        }
                        if (presentation.swapchain == VK_NULL_HANDLE)
                        {
                            return {};
                        }
                    }

                    auto presented = PresentOnce(presentation);
                    if (presented == VK_SUCCESS)
                    {
                        return {};
                    }
                    if (presented != VK_ERROR_OUT_OF_DATE_KHR &&
                        presented != VK_SUBOPTIMAL_KHR)
                    {
                        presentation.failed = true;
                        return std::unexpected(VulkanFailure(
                            "Presenting a Vulkan swapchain image",
                            presented));
                    }

                    auto recreated =
                        RecreateSwapchain(
                            presentation,
                            framebufferSize);
                    if (!recreated)
                    {
                        return recreated;
                    }
                }

                return std::unexpected(PresentationFailure(
                    "The Vulkan swapchain remained out of date after "
                    "recreation."));
            }

            WindowPresentationResult RecreateSwapchain(
                PresentedWindow& presentation,
                FramebufferSize requestedSize)
            {
                VkSurfaceCapabilitiesKHR capabilities {};
                auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physicalDevice,
                    presentation.surface,
                    &capabilities);
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        "Querying Vulkan surface capabilities",
                        result));
                }

                const auto extent =
                    ChooseExtent(capabilities, requestedSize);
                if (extent.width == 0 || extent.height == 0)
                {
                    DestroySwapchain(presentation);
                    return {};
                }
                if ((capabilities.supportedUsageFlags &
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
                {
                    return std::unexpected(PresentationFailure(
                        "The Vulkan surface does not support transfer clears "
                        "for swapchain images."));
                }

                auto formats =
                    SurfaceFormats(physicalDevice, presentation.surface);
                if (!formats)
                {
                    return std::unexpected(std::move(formats.error()));
                }
                if (formats->empty())
                {
                    return std::unexpected(PresentationFailure(
                        "The Vulkan surface exposes no image formats."));
                }
                auto modes =
                    PresentModes(physicalDevice, presentation.surface);
                if (!modes)
                {
                    return std::unexpected(std::move(modes.error()));
                }
                if (modes->empty())
                {
                    return std::unexpected(PresentationFailure(
                        "The Vulkan surface exposes no presentation modes."));
                }

                const auto format = ChooseSurfaceFormat(*formats);
                const auto presentMode = ChoosePresentMode(*modes);
                std::uint32_t imageCount =
                    capabilities.minImageCount + 1;
                if (capabilities.maxImageCount > 0)
                {
                    imageCount = std::min(
                        imageCount,
                        capabilities.maxImageCount);
                }

                const auto compositeAlpha =
                    ChooseCompositeAlpha(
                        capabilities.supportedCompositeAlpha);
                std::array familyIndices {
                    queueFamilies.graphics,
                    queueFamilies.present
                };
                const bool concurrent =
                    queueFamilies.graphics != queueFamilies.present;
                const VkSwapchainCreateInfoKHR createInfo {
                    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .flags = 0,
                    .surface = presentation.surface,
                    .minImageCount = imageCount,
                    .imageFormat = format.format,
                    .imageColorSpace = format.colorSpace,
                    .imageExtent = extent,
                    .imageArrayLayers = 1,
                    .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    .imageSharingMode = concurrent
                        ? VK_SHARING_MODE_CONCURRENT
                        : VK_SHARING_MODE_EXCLUSIVE,
                    .queueFamilyIndexCount = concurrent ? 2U : 0U,
                    .pQueueFamilyIndices = concurrent
                        ? familyIndices.data()
                        : nullptr,
                    .preTransform = capabilities.currentTransform,
                    .compositeAlpha = compositeAlpha,
                    .presentMode = presentMode,
                    .clipped = VK_TRUE,
                    .oldSwapchain = presentation.swapchain
                };

                result = vkDeviceWaitIdle(device);
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        "Waiting for the Vulkan device before replacing a "
                        "swapchain",
                        result));
                }
                result = WaitForPresentation(presentation);
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        "Waiting for Vulkan presentation before replacing a "
                        "swapchain",
                        result));
                }

                PendingSwapchainResources replacement;
                replacement.device = device;
                result = vkCreateSwapchainKHR(
                    device,
                    &createInfo,
                    nullptr,
                    &replacement.swapchain);

                // oldSwapchain is retired by the creation attempt even when
                // creation fails, so it must never remain published here.
                DestroySwapchain(presentation, false);
                if (result != VK_SUCCESS)
                {
                    return std::unexpected(VulkanFailure(
                        "Creating the Vulkan swapchain",
                        result));
                }

                auto images = VulkanVector<VkImage>(
                    "Querying Vulkan swapchain images",
                    [this, swapchain = replacement.swapchain](
                        std::uint32_t* count,
                        VkImage* values)
                    {
                        return vkGetSwapchainImagesKHR(
                            device,
                            swapchain,
                            count,
                            values);
                    });
                if (!images)
                {
                    return std::unexpected(std::move(images.error()));
                }
                if (images->empty())
                {
                    return std::unexpected(PresentationFailure(
                        "The Vulkan swapchain exposes no images."));
                }

                replacement.format = format.format;
                replacement.extent = extent;
                replacement.images = std::move(*images);
                replacement.renderFinished.assign(
                    replacement.images.size(),
                    VK_NULL_HANDLE);
                replacement.presentationComplete.assign(
                    replacement.images.size(),
                    VK_NULL_HANDLE);
                replacement.presentationPending.assign(
                    replacement.images.size(),
                    false);
                replacement.initialized.assign(
                    replacement.images.size(),
                    false);
                replacement.imagesInFlight.assign(
                    replacement.images.size(),
                    VK_NULL_HANDLE);

                const VkSemaphoreCreateInfo semaphoreInfo {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0
                };
                const VkFenceCreateInfo fenceInfo {
                    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0
                };
                for (std::size_t index = 0;
                     index < replacement.images.size();
                     ++index)
                {
                    result = vkCreateSemaphore(
                        device,
                        &semaphoreInfo,
                        nullptr,
                        &replacement.renderFinished[index]);
                    if (result != VK_SUCCESS)
                    {
                        return std::unexpected(VulkanFailure(
                            "Creating Vulkan presentation semaphores",
                            result));
                    }
                    result = vkCreateFence(
                        device,
                        &fenceInfo,
                        nullptr,
                        &replacement.presentationComplete[index]);
                    if (result != VK_SUCCESS)
                    {
                        return std::unexpected(VulkanFailure(
                            "Creating Vulkan presentation fences",
                            result));
                    }
                }

                presentation.swapchain = replacement.swapchain;
                presentation.format = replacement.format;
                presentation.extent = replacement.extent;
                presentation.images = std::move(replacement.images);
                presentation.renderFinished =
                    std::move(replacement.renderFinished);
                presentation.presentationComplete =
                    std::move(replacement.presentationComplete);
                presentation.presentationPending =
                    std::move(replacement.presentationPending);
                presentation.initialized =
                    std::move(replacement.initialized);
                presentation.imagesInFlight =
                    std::move(replacement.imagesInFlight);
                presentation.frameIndex = 0;
                presentation.failed = false;

                replacement.device = VK_NULL_HANDLE;
                replacement.swapchain = VK_NULL_HANDLE;
                return {};
            }

            VkResult PresentOnce(PresentedWindow& presentation)
            {
                auto& frame =
                    presentation.frames[presentation.frameIndex];
                auto result = vkWaitForFences(
                    device,
                    1,
                    &frame.complete,
                    VK_TRUE,
                    std::numeric_limits<std::uint64_t>::max());
                if (result != VK_SUCCESS)
                {
                    return result;
                }

                std::uint32_t imageIndex = 0;
                result = vkAcquireNextImageKHR(
                    device,
                    presentation.swapchain,
                    std::numeric_limits<std::uint64_t>::max(),
                    frame.imageAvailable,
                    VK_NULL_HANDLE,
                    &imageIndex);
                const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
                if (result != VK_SUCCESS && !suboptimal)
                {
                    return result;
                }
                if (imageIndex >= presentation.images.size())
                {
                    return VK_ERROR_UNKNOWN;
                }
                const auto renderFinished =
                    presentation.renderFinished[imageIndex];
                const auto presentationComplete =
                    presentation.presentationComplete[imageIndex];

                const auto previousFence =
                    presentation.imagesInFlight[imageIndex];
                if (previousFence != VK_NULL_HANDLE &&
                    previousFence != frame.complete)
                {
                    result = vkWaitForFences(
                        device,
                        1,
                        &previousFence,
                        VK_TRUE,
                        std::numeric_limits<std::uint64_t>::max());
                    if (result != VK_SUCCESS)
                    {
                        return result;
                    }
                }
                if (presentation.presentationPending[imageIndex])
                {
                    result = vkWaitForFences(
                        device,
                        1,
                        &presentationComplete,
                        VK_TRUE,
                        std::numeric_limits<std::uint64_t>::max());
                    if (result != VK_SUCCESS)
                    {
                        return result;
                    }
                    result = vkResetFences(
                        device,
                        1,
                        &presentationComplete);
                    if (result != VK_SUCCESS)
                    {
                        return result;
                    }
                    presentation.presentationPending[imageIndex] = false;
                }

                result = vkResetCommandBuffer(
                    frame.commandBuffer,
                    0);
                if (result != VK_SUCCESS)
                {
                    return result;
                }
                result = RecordClear(
                    frame.commandBuffer,
                    presentation.images[imageIndex],
                    presentation.initialized[imageIndex]);
                if (result != VK_SUCCESS)
                {
                    return result;
                }

                result = vkResetFences(device, 1, &frame.complete);
                if (result != VK_SUCCESS)
                {
                    return result;
                }

                const VkPipelineStageFlags waitStage =
                    VK_PIPELINE_STAGE_TRANSFER_BIT;
                const VkSubmitInfo submitInfo {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext = nullptr,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &frame.imageAvailable,
                    .pWaitDstStageMask = &waitStage,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &frame.commandBuffer,
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores = &renderFinished
                };
                result = vkQueueSubmit(
                    graphicsQueue,
                    1,
                    &submitInfo,
                    frame.complete);
                if (result != VK_SUCCESS)
                {
                    return result;
                }

                presentation.imagesInFlight[imageIndex] = frame.complete;
                presentation.initialized[imageIndex] = true;

                const VkSwapchainPresentFenceInfoEXT presentFenceInfo {
                    .sType =
                        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
                    .pNext = nullptr,
                    .swapchainCount = 1,
                    .pFences = &presentationComplete
                };
                const VkPresentInfoKHR presentInfo {
                    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                    .pNext = &presentFenceInfo,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &renderFinished,
                    .swapchainCount = 1,
                    .pSwapchains = &presentation.swapchain,
                    .pImageIndices = &imageIndex,
                    .pResults = nullptr
                };
                result = vkQueuePresentKHR(
                    presentQueue,
                    &presentInfo);
                if (PresentWasEnqueued(result))
                {
                    presentation.presentationPending[imageIndex] = true;
                }
                presentation.frameIndex =
                    (presentation.frameIndex + 1) % FramesInFlight;
                if (suboptimal && result == VK_SUCCESS)
                {
                    return VK_SUBOPTIMAL_KHR;
                }
                return result;
            }

            static bool PresentWasEnqueued(VkResult result) noexcept
            {
                return result == VK_SUCCESS ||
                    result == VK_SUBOPTIMAL_KHR ||
                    result == VK_ERROR_OUT_OF_DATE_KHR ||
                    result == VK_ERROR_SURFACE_LOST_KHR;
            }

            static VkResult RecordClear(
                VkCommandBuffer commandBuffer,
                VkImage image,
                bool initialized)
            {
                const VkCommandBufferBeginInfo beginInfo {
                    .sType =
                        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .pNext = nullptr,
                    .flags =
                        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                    .pInheritanceInfo = nullptr
                };
                auto result =
                    vkBeginCommandBuffer(commandBuffer, &beginInfo);
                if (result != VK_SUCCESS)
                {
                    return result;
                }

                const VkImageMemoryBarrier beginBarrier {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .oldLayout = initialized
                        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                        : VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = image,
                    .subresourceRange = VkImageSubresourceRange {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                    }
                };
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    1,
                    &beginBarrier);

                const VkClearColorValue clearColor {
                    .float32 = { 0.025F, 0.03F, 0.04F, 1.0F }
                };
                const VkImageSubresourceRange range {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                };
                vkCmdClearColorImage(
                    commandBuffer,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    &clearColor,
                    1,
                    &range);

                const VkImageMemoryBarrier endBarrier {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = 0,
                    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = image,
                    .subresourceRange = range
                };
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    1,
                    &endBarrier);

                return vkEndCommandBuffer(commandBuffer);
            }

            VkResult WaitForPresentation(
                PresentedWindow& presentation) noexcept
            {
                for (std::size_t index = 0;
                     index < presentation.presentationPending.size();
                     ++index)
                {
                    if (!presentation.presentationPending[index])
                    {
                        continue;
                    }

                    const auto result = vkWaitForFences(
                        device,
                        1,
                        &presentation.presentationComplete[index],
                        VK_TRUE,
                        std::numeric_limits<std::uint64_t>::max());
                    if (result != VK_SUCCESS)
                    {
                        return result;
                    }
                    presentation.presentationPending[index] = false;
                }
                return VK_SUCCESS;
            }

            void DestroyPresentedWindow(
                PresentedWindow& presentation,
                bool waitForDevice = true) noexcept
            {
                if (device != VK_NULL_HANDLE && waitForDevice)
                {
                    (void)vkDeviceWaitIdle(device);
                }
                DestroySwapchain(presentation, false);

                if (device != VK_NULL_HANDLE)
                {
                    std::array<VkCommandBuffer, FramesInFlight> buffers {};
                    std::uint32_t bufferCount = 0;
                    for (auto& frame : presentation.frames)
                    {
                        if (frame.complete != VK_NULL_HANDLE)
                        {
                            vkDestroyFence(
                                device,
                                frame.complete,
                                nullptr);
                            frame.complete = VK_NULL_HANDLE;
                        }
                        if (frame.imageAvailable != VK_NULL_HANDLE)
                        {
                            vkDestroySemaphore(
                                device,
                                frame.imageAvailable,
                                nullptr);
                            frame.imageAvailable = VK_NULL_HANDLE;
                        }
                        if (frame.commandBuffer != VK_NULL_HANDLE)
                        {
                            buffers[bufferCount++] = frame.commandBuffer;
                            frame.commandBuffer = VK_NULL_HANDLE;
                        }
                    }
                    if (bufferCount > 0 &&
                        commandPool != VK_NULL_HANDLE)
                    {
                        vkFreeCommandBuffers(
                            device,
                            commandPool,
                            bufferCount,
                            buffers.data());
                    }
                }

                if (presentation.surface != VK_NULL_HANDLE &&
                    instance != VK_NULL_HANDLE)
                {
                    vkDestroySurfaceKHR(
                        instance,
                        presentation.surface,
                        nullptr);
                    presentation.surface = VK_NULL_HANDLE;
                }
            }

            void DestroySwapchain(
                PresentedWindow& presentation,
                bool waitForDevice = true) noexcept
            {
                if (device != VK_NULL_HANDLE && waitForDevice &&
                    (presentation.swapchain != VK_NULL_HANDLE ||
                     !presentation.renderFinished.empty()))
                {
                    (void)vkDeviceWaitIdle(device);
                }
                (void)WaitForPresentation(presentation);
                DestroyPresentationSync(presentation);

                if (presentation.swapchain == VK_NULL_HANDLE)
                {
                    presentation.extent = {};
                    presentation.images.clear();
                    presentation.initialized.clear();
                    presentation.imagesInFlight.clear();
                    presentation.presentationPending.clear();
                    return;
                }
                vkDestroySwapchainKHR(
                    device,
                    presentation.swapchain,
                    nullptr);
                presentation.swapchain = VK_NULL_HANDLE;
                presentation.format = VK_FORMAT_UNDEFINED;
                presentation.extent = {};
                presentation.images.clear();
                presentation.initialized.clear();
                presentation.imagesInFlight.clear();
                presentation.presentationPending.clear();
                presentation.frameIndex = 0;
            }

            void DestroyPresentationSync(
                PresentedWindow& presentation) noexcept
            {
                if (device != VK_NULL_HANDLE)
                {
                    for (const auto fence :
                         presentation.presentationComplete)
                    {
                        if (fence != VK_NULL_HANDLE)
                        {
                            vkDestroyFence(
                                device,
                                fence,
                                nullptr);
                        }
                    }
                    for (const auto semaphore :
                         presentation.renderFinished)
                    {
                        if (semaphore != VK_NULL_HANDLE)
                        {
                            vkDestroySemaphore(
                                device,
                                semaphore,
                                nullptr);
                        }
                    }
                }
                presentation.presentationComplete.clear();
                presentation.renderFinished.clear();
            }

            static VkExtent2D ChooseExtent(
                const VkSurfaceCapabilitiesKHR& capabilities,
                FramebufferSize requested)
            {
                if (capabilities.currentExtent.width !=
                    std::numeric_limits<std::uint32_t>::max())
                {
                    return capabilities.currentExtent;
                }
                return VkExtent2D {
                    .width = std::clamp(
                        requested.width,
                        capabilities.minImageExtent.width,
                        capabilities.maxImageExtent.width),
                    .height = std::clamp(
                        requested.height,
                        capabilities.minImageExtent.height,
                        capabilities.maxImageExtent.height)
                };
            }

            static VkSurfaceFormatKHR ChooseSurfaceFormat(
                const std::vector<VkSurfaceFormatKHR>& formats)
            {
                if (formats.size() == 1 &&
                    formats.front().format == VK_FORMAT_UNDEFINED)
                {
                    return VkSurfaceFormatKHR {
                        .format = VK_FORMAT_B8G8R8A8_UNORM,
                        .colorSpace =
                            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
                    };
                }

                constexpr std::array preferredFormats {
                    VK_FORMAT_B8G8R8A8_SRGB,
                    VK_FORMAT_R8G8B8A8_SRGB,
                    VK_FORMAT_B8G8R8A8_UNORM,
                    VK_FORMAT_R8G8B8A8_UNORM
                };
                for (const auto preferred : preferredFormats)
                {
                    const auto found = std::ranges::find_if(
                        formats,
                        [preferred](const VkSurfaceFormatKHR& format)
                        {
                            return format.format == preferred &&
                                format.colorSpace ==
                                    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                        });
                    if (found != formats.end())
                    {
                        return *found;
                    }
                }
                return formats.front();
            }

            static VkPresentModeKHR ChoosePresentMode(
                const std::vector<VkPresentModeKHR>& modes)
            {
                const auto fifo = std::ranges::find(
                    modes,
                    VK_PRESENT_MODE_FIFO_KHR);
                return fifo != modes.end()
                    ? *fifo
                    : modes.front();
            }

            static VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(
                VkCompositeAlphaFlagsKHR supported)
            {
                constexpr std::array choices {
                    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
                };
                for (const auto choice : choices)
                {
                    if ((supported & choice) != 0)
                    {
                        return choice;
                    }
                }
                return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            }

            static std::expected<
                std::vector<VkSurfaceFormatKHR>,
                WindowError>
            SurfaceFormats(
                VkPhysicalDevice candidate,
                VkSurfaceKHR surface)
            {
                return VulkanVector<VkSurfaceFormatKHR>(
                    "Querying Vulkan surface formats",
                    [candidate, surface](
                        std::uint32_t* count,
                        VkSurfaceFormatKHR* values)
                    {
                        return vkGetPhysicalDeviceSurfaceFormatsKHR(
                            candidate,
                            surface,
                            count,
                            values);
                    });
            }

            static std::expected<
                std::vector<VkPresentModeKHR>,
                WindowError>
            PresentModes(
                VkPhysicalDevice candidate,
                VkSurfaceKHR surface)
            {
                return VulkanVector<VkPresentModeKHR>(
                    "Querying Vulkan presentation modes",
                    [candidate, surface](
                        std::uint32_t* count,
                        VkPresentModeKHR* values)
                    {
                        return vkGetPhysicalDeviceSurfacePresentModesKHR(
                            candidate,
                            surface,
                            count,
                            values);
                    });
            }

            VkInstance instance { VK_NULL_HANDLE };
            VkPhysicalDevice physicalDevice { VK_NULL_HANDLE };
            VkDevice device { VK_NULL_HANDLE };
            QueueFamilies queueFamilies;
            VkQueue graphicsQueue { VK_NULL_HANDLE };
            VkQueue presentQueue { VK_NULL_HANDLE };
            VkCommandPool commandPool { VK_NULL_HANDLE };
            std::unordered_map<WindowId, PresentedWindow> windows;
            std::unordered_set<WindowId> noPresentationWindows;
            bool stopped { false };
        };
    }

    std::unique_ptr<IWindowPresenter> CreateVulkanWindowPresenter()
    {
        return std::make_unique<VulkanWindowPresenter>();
    }
}
