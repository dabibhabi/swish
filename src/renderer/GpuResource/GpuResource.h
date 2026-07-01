#pragma once

// Move-only RAII wrappers around VMA-allocated buffers and images. The
// destructor returns the sub-allocation to the VMA allocator, so callers no
// longer pair a manual vkDestroyBuffer/Image with a vkFreeMemory (and no longer
// do one vkAllocateMemory per resource — VMA sub-allocates from big blocks).
//
// A wrapper stores the allocator it was created from, so it must be destroyed
// (or reset) BEFORE Device destroys that allocator.

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <utility>

namespace swish {

class GpuBuffer {
public:
    GpuBuffer() = default;

    GpuBuffer(VmaAllocator allocator, const VkBufferCreateInfo& bufferInfo,
              const VmaAllocationCreateInfo& allocInfo)
        : m_allocator(allocator) {
        vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation, &m_info);
    }

    ~GpuBuffer() { reset(); }

    GpuBuffer(GpuBuffer&& o) noexcept { swap(o); }
    GpuBuffer& operator=(GpuBuffer&& o) noexcept {
        if (this != &o) {
            reset();
            swap(o);
        }
        return *this;
    }
    GpuBuffer(const GpuBuffer&)            = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    VkBuffer      handle() const { return m_buffer; }
    VmaAllocation allocation() const { return m_allocation; }
    // Persistently-mapped pointer (valid only when created with MAPPED + HOST_ACCESS).
    void*         mapped() const { return m_info.pMappedData; }
    explicit      operator bool() const { return m_buffer != VK_NULL_HANDLE; }

    void reset() {
        if (m_buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer     = VK_NULL_HANDLE;
        m_allocation = nullptr;
        m_info       = {};
    }

private:
    void swap(GpuBuffer& o) noexcept {
        std::swap(m_allocator, o.m_allocator);
        std::swap(m_buffer, o.m_buffer);
        std::swap(m_allocation, o.m_allocation);
        std::swap(m_info, o.m_info);
    }

    VmaAllocator      m_allocator  = nullptr;
    VkBuffer          m_buffer     = VK_NULL_HANDLE;
    VmaAllocation     m_allocation = nullptr;
    VmaAllocationInfo m_info{};
};

class GpuImage {
public:
    GpuImage() = default;

    GpuImage(VmaAllocator allocator, const VkImageCreateInfo& imageInfo,
             const VmaAllocationCreateInfo& allocInfo)
        : m_allocator(allocator) {
        vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr);
    }

    ~GpuImage() { reset(); }

    GpuImage(GpuImage&& o) noexcept { swap(o); }
    GpuImage& operator=(GpuImage&& o) noexcept {
        if (this != &o) {
            reset();
            swap(o);
        }
        return *this;
    }
    GpuImage(const GpuImage&)            = delete;
    GpuImage& operator=(const GpuImage&) = delete;

    VkImage       handle() const { return m_image; }
    VmaAllocation allocation() const { return m_allocation; }
    explicit      operator bool() const { return m_image != VK_NULL_HANDLE; }

    void reset() {
        if (m_image != VK_NULL_HANDLE)
            vmaDestroyImage(m_allocator, m_image, m_allocation);
        m_image      = VK_NULL_HANDLE;
        m_allocation = nullptr;
    }

private:
    void swap(GpuImage& o) noexcept {
        std::swap(m_allocator, o.m_allocator);
        std::swap(m_image, o.m_image);
        std::swap(m_allocation, o.m_allocation);
    }

    VmaAllocator  m_allocator  = nullptr;
    VkImage       m_image      = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;
};

// ── Factory helpers for the common allocation patterns ────────────────────
namespace gpu {

// GPU-only buffer (vertex/index after a staging copy).
inline GpuBuffer deviceLocalBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    return GpuBuffer(allocator, bi, ai);
}

// Host-visible, persistently-mapped buffer (uniforms, staging, CPU-written data).
// Access .mapped() for the pointer.
inline GpuBuffer hostVisibleBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    return GpuBuffer(allocator, bi, ai);
}

// Device-local 2D image (textures, attachments, depth).
inline GpuImage deviceLocalImage(VmaAllocator allocator, uint32_t width, uint32_t height, VkFormat format,
                                 VkImageTiling tiling, VkImageUsageFlags usage) {
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.extent        = {width, height, 1};
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.format        = format;
    ii.tiling        = tiling;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage         = usage;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    return GpuImage(allocator, ii, ai);
}

}  // namespace gpu

}  // namespace swish
