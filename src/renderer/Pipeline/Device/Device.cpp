#include "Device.h"

#include "../../../utils/VulkanCheck.h"
#include "../Pipeline.h"

#include <vk_mem_alloc.h>

#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace swish {

// config/pipeline_cache.bin lives in the build tree (CONFIG_DIR = ${CMAKE_BINARY_DIR}/config/).
static const std::string kPipelineCachePath = std::string(CONFIG_DIR) + "pipeline_cache.bin";

void Device::init(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (rateDevice(device, surface) > 0) {
            m_physicalDevice = device;
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("no suitable GPU found — check Vulkan drivers and extensions");
    }

    // Cache queue families so callers don't have to re-enumerate
    m_queueFamilies = findQueueFamilies(m_physicalDevice, surface);

    // Build one VkDeviceQueueCreateInfo per unique family index so that the
    // present and graphics queues are both created when they differ.
    std::set<uint32_t> uniqueFamilies = {
        m_queueFamilies.graphicsFamily.value(),
        m_queueFamilies.presentFamily.value(),
    };

    float                                queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueFamilies.size());
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(qci);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos    = queueCreateInfos.data();

    VkPhysicalDeviceFeatures deviceFeatures{};
    createInfo.pEnabledFeatures = &deviceFeatures;

    std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    for (const auto& extension : availableExtensions) {
        if (strcmp(extension.extensionName, "VK_KHR_portability_subset") == 0) {
            deviceExtensions.push_back("VK_KHR_portability_subset");
            break;
        }
    }

    createInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(m_device, m_queueFamilies.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilies.presentFamily.value(), 0, &m_presentQueue);

    // Create the single VMA allocator for the whole renderer. Must match the
    // instance's API version (VK_API_VERSION_1_3, see VulkanContext).
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance         = instance;
    allocatorInfo.physicalDevice   = m_physicalDevice;
    allocatorInfo.device           = m_device;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VMA allocator!");
    }

    // Pipeline cache: created here so every subsequent Pipeline::create (all of
    // which run after Device::init) feeds it.
    createPipelineCache();
    Pipeline::set_cache(m_pipelineCache);
}

void Device::cleanup() {
    // Persist + destroy the pipeline cache before the device (pipelines that used
    // it were already destroyed by their subsystems' cleanup).
    if (m_pipelineCache != VK_NULL_HANDLE) {
        Pipeline::set_cache(VK_NULL_HANDLE);
        savePipelineCache();
        vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
        m_pipelineCache = VK_NULL_HANDLE;
    }
    // Destroy the allocator (and thus its pooled device memory) before the
    // logical device it was created from.
    if (m_allocator != nullptr) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = nullptr;
    }
    vkDestroyDevice(m_device, nullptr);
}

void Device::createPipelineCache() {
    // Seed with the previously-saved blob if present. Vulkan validates the blob's
    // header (vendor/device/UUID) and silently ignores it on mismatch, so a stale
    // or foreign cache is safe — it just falls back to compiling from scratch.
    std::vector<char> initialData;
    if (std::ifstream in{kPipelineCachePath, std::ios::binary | std::ios::ate}) {
        const std::streamsize size = in.tellg();
        if (size > 0) {
            initialData.resize(static_cast<size_t>(size));
            in.seekg(0);
            in.read(initialData.data(), size);
        }
    }

    VkPipelineCacheCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = initialData.size();
    info.pInitialData    = initialData.empty() ? nullptr : initialData.data();
    VK_CHECK(vkCreatePipelineCache(m_device, &info, nullptr, &m_pipelineCache));
}

void Device::savePipelineCache() const {
    size_t size = 0;
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0)
        return;
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data()) != VK_SUCCESS)
        return;
    if (std::ofstream out{kPipelineCachePath, std::ios::binary | std::ios::trunc})
        out.write(data.data(), static_cast<std::streamsize>(size));
}

VkPhysicalDevice Device::getPhysicalDevice() const {
    return m_physicalDevice;
}

VkDevice Device::getDevice() const {
    return m_device;
}

VkQueue Device::getGraphicsQueue() const {
    return m_graphicsQueue;
}

VkQueue Device::getPresentQueue() const {
    return m_presentQueue;
}

const QueueFamilyIndices& Device::getQueueFamilies() const {
    return m_queueFamilies;
}

QueueFamilyIndices Device::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    QueueFamilyIndices indices;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
    }
    return indices;
}

SwapChainSupportDetails Device::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) const {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    return details;
}

int Device::rateDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const {
    // Must support required extensions (swapchain at minimum)
    if (!checkDeviceExtensionSupport(device))
        return 0;

    // Must expose both a graphics and a present queue
    if (!findQueueFamilies(device, surface).isComplete())
        return 0;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);

    int score = 0;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score = 1000;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score = 100;
    }

    // Prefer the vendor that matches the configured backend
#if defined(SWISH_BACKEND_LINUX) || defined(SWISH_BACKEND_WINDOWS)
    if (properties.vendorID == 0x1002 || properties.vendorID == 0x10DE)
        score += 500;  // AMD / NVIDIA
#elif defined(SWISH_BACKEND_APPLE)
    if (properties.vendorID == 0x106B)
        score += 500;  // Apple GPU
#endif

    return score;
}

bool Device::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    for (const auto& extension : extensions) {
        if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace swish
