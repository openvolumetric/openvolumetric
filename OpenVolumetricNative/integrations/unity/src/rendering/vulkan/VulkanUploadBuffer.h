#pragma once

#include <Unity/IUnityGraphicsVulkan.h>

#include <cstddef>

namespace openvolumetric::unity
{

/// Persistently mapped, host-visible Vulkan staging buffer.
class VulkanUploadBuffer
{
public:
    /// Constructs an empty staging allocation.
    VulkanUploadBuffer() = default;
    /// Unmaps and releases any live allocation.
    ~VulkanUploadBuffer();

    /// Creates and persistently maps a coherent transfer-source buffer.
    bool create(
        const UnityVulkanInstance& instance,
        VkDeviceSize size);
    /// Releases the mapping, memory, and buffer in dependency order.
    void destroy();

    /// Returns the Vulkan transfer-source buffer.
    VkBuffer buffer() const { return m_buffer; }
    /// Returns the CPU mapping used to populate transfer data.
    void* mapped() const { return m_mapped; }
    /// Returns the allocation capacity in bytes.
    VkDeviceSize size() const { return m_size; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
    VkDeviceSize m_size = 0;
};

} // namespace openvolumetric::unity
