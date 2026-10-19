#include "utils/io/AppPaths.h"
#include "Device.h"

#include "utils/vulkan/Check.h"

#include <vk_mem_alloc.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace swish {

// config/pipeline_cache.bin: per-user cache dir, never inside an app bundle (see utils/io/AppPaths.h).
static std::string pipeline_cache_path() { return util::paths::user_cache_dir() + "pipeline_cache.bin"; }

void Device::init(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    int bestScore = 0;
    for (const auto& device : devices) {
        const int score = rateDevice(device, surface);
        if (score > bestScore) {
            bestScore        = score;
            m_physicalDevice = device;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("no suitable GPU found — check Vulkan drivers and extensions");
    }

    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        std::cerr << "Vulkan device: " << props.deviceName << " (score=" << bestScore << ")\n";
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

    // Vulkan 1.3 features (dynamic rendering + synchronization2). Query, then enable what the device
    // supports; unsupported is not an error (classic render pass + barrier path stays available).
    // SWISH_CLASSIC_VULKAN=1 forces the classic path for testing both code paths on one machine.
    VkPhysicalDeviceProperties devProps{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &devProps);
    const char* forceClassic  = std::getenv("SWISH_CLASSIC_VULKAN");
    const bool  classicForced = forceClassic != nullptr && forceClassic[0] != '\0' && forceClassic[0] != '0';

    VkPhysicalDeviceVulkan13Features supported13{};
    supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    if (devProps.apiVersion >= VK_API_VERSION_1_3) {  // the 1.3 feature struct is only valid on a 1.3 device
        VkPhysicalDeviceFeatures2 query{};
        query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        query.pNext = &supported13;
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &query);
    }

    m_dynamicRendering = !classicForced && supported13.dynamicRendering == VK_TRUE;
    m_synchronization2 = !classicForced && supported13.synchronization2 == VK_TRUE;

    VkPhysicalDeviceVulkan13Features enabled13{};
    enabled13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabled13.dynamicRendering = m_dynamicRendering ? VK_TRUE : VK_FALSE;
    enabled13.synchronization2 = m_synchronization2 ? VK_TRUE : VK_FALSE;

    // pEnabledFeatures and a VkPhysicalDeviceFeatures2 in pNext are mutually exclusive. With no 1.3
    // feature to enable the create info is exactly what it was before this change.
    VkPhysicalDeviceFeatures2 enabledFeatures2{};
    if (m_dynamicRendering || m_synchronization2) {
        enabledFeatures2.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        enabledFeatures2.pNext      = &enabled13;
        enabledFeatures2.features   = deviceFeatures;  // all core features stay off, as before
        createInfo.pNext            = &enabledFeatures2;
        createInfo.pEnabledFeatures = nullptr;
    }

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

    std::cout << "[Device] " << devProps.deviceName << " (Vulkan " << VK_VERSION_MAJOR(devProps.apiVersion) << '.'
              << VK_VERSION_MINOR(devProps.apiVersion) << '.' << VK_VERSION_PATCH(devProps.apiVersion)
              << "): dynamicRendering=" << (m_dynamicRendering ? "on" : "off")
              << " synchronization2=" << (m_synchronization2 ? "on" : "off") << " -> "
              << ((m_dynamicRendering || m_synchronization2) ? "Vulkan 1.3 path" : "classic render pass path")
              << (classicForced ? " (forced by SWISH_CLASSIC_VULKAN)" : "") << '\n';

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

    // Pipeline cache: created here and handed out via getPipelineCache() (RendererServices::pipelineCache), so
    // every pipeline::create_* that runs after Device::init feeds it.
    createPipelineCache();
}

void Device::cleanup() {
    // Persist + destroy the pipeline cache before the device (pipelines that used
    // it were already destroyed by their subsystems' cleanup).
    if (m_pipelineCache != VK_NULL_HANDLE) {
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
    if (std::ifstream in{pipeline_cache_path(), std::ios::binary | std::ios::ate}) {
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
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0) return;
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data()) != VK_SUCCESS) return;
    if (std::ofstream out{pipeline_cache_path(), std::ios::binary | std::ios::trunc})
        out.write(data.data(), static_cast<std::streamsize>(size));
}

VkPhysicalDevice Device::getPhysicalDevice() const { return m_physicalDevice; }

VkDevice Device::getDevice() const { return m_device; }

VkQueue Device::getGraphicsQueue() const { return m_graphicsQueue; }

VkQueue Device::getPresentQueue() const { return m_presentQueue; }

const QueueFamilyIndices& Device::getQueueFamilies() const { return m_queueFamilies; }

QueueFamilyIndices Device::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    QueueFamilyIndices indices;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { indices.graphicsFamily = i; }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) { indices.presentFamily = i; }
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
    if (!checkDeviceExtensionSupport(device)) return 0;

    // Must expose both a graphics and a present queue
    if (!findQueueFamilies(device, surface).isComplete()) return 0;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);

    int score = 0;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score = 1000;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score = 100;
    }

    // Prefer the vendor that matches the configured backend
#if defined(SWISH_BACKEND_LINUX)
    if (properties.vendorID == 0x1002 || properties.vendorID == 0x10DE) score += 500;  // AMD / NVIDIA
#elif defined(SWISH_BACKEND_APPLE)
    if (properties.vendorID == 0x106B) score += 500;  // Apple GPU
#endif

    return score;
}

bool Device::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    for (const auto& extension : extensions) {
        if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { return true; }
    }
    return false;
}

}  // namespace swish
