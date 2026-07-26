#include "VulkanUploadBuffer.h"

#include <Logger.h>

namespace
{
bool find_memory_type(
    VkPhysicalDevice physical_device,
    std::uint32_t type_bits,
    VkMemoryPropertyFlags required,
    std::uint32_t& result)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    {
        if ((type_bits & (1u << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & required) == required)
        {
            result = i;
            return true;
        }
    }
    return false;
}
}

VulkanUploadBuffer::~VulkanUploadBuffer()
{
    destroy();
}

bool VulkanUploadBuffer::create(
    const UnityVulkanInstance& instance,
    VkDeviceSize size)
{
    destroy();
    if (instance.device == VK_NULL_HANDLE || size == 0)
        return false;

    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(
            instance.device, &buffer_info, nullptr, &m_buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(
        instance.device, m_buffer, &requirements);

    std::uint32_t memory_type = 0;
    const VkMemoryPropertyFlags flags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!find_memory_type(
            instance.physicalDevice,
            requirements.memoryTypeBits,
            flags,
            memory_type))
    {
        LOG("VulkanUploadBuffer::create - no coherent host memory");
        destroy();
        return false;
    }

    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(
            instance.device, &allocation, nullptr, &m_memory) != VK_SUCCESS ||
        vkBindBufferMemory(
            instance.device, m_buffer, m_memory, 0) != VK_SUCCESS ||
        vkMapMemory(
            instance.device,
            m_memory,
            0,
            requirements.size,
            0,
            &m_mapped) != VK_SUCCESS)
    {
        destroy();
        return false;
    }

    m_device = instance.device;
    m_size = size;
    return true;
}

void VulkanUploadBuffer::destroy()
{
    if (m_device != VK_NULL_HANDLE && m_memory != VK_NULL_HANDLE &&
        m_mapped != nullptr)
        vkUnmapMemory(m_device, m_memory);
    if (m_device != VK_NULL_HANDLE && m_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device, m_buffer, nullptr);
    if (m_device != VK_NULL_HANDLE && m_memory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, m_memory, nullptr);

    m_device = VK_NULL_HANDLE;
    m_buffer = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    m_mapped = nullptr;
    m_size = 0;
}
