#pragma once

#include <Unity/IUnityGraphicsVulkan.h>

#include <cstddef>

class VulkanUploadBuffer
{
public:
    VulkanUploadBuffer() = default;
    ~VulkanUploadBuffer();

    bool create(
        const UnityVulkanInstance& instance,
        VkDeviceSize size);
    void destroy();

    VkBuffer buffer() const { return m_buffer; }
    void* mapped() const { return m_mapped; }
    VkDeviceSize size() const { return m_size; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
    VkDeviceSize m_size = 0;
};
