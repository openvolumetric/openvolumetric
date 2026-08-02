#include "TextureVulkan.h"

#include <Logger.h>

#include <cstring>

namespace
{
bool find_device_memory(
    VkPhysicalDevice physical_device,
    std::uint32_t type_bits,
    std::uint32_t& result)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    {
        if ((type_bits & (1u << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            result = i;
            return true;
        }
    }
    return false;
}
}

namespace openvolumetric::unity
{

TextureVulkan::TextureVulkan() = default;

TextureVulkan::~TextureVulkan()
{
    destroy();
}


int TextureVulkan::init(
    void* handler,
    unsigned int width,
    unsigned int height)
{
    destroy();
    if (handler == nullptr || width == 0 || height == 0)
        return -1;

    m_unity_vulkan = static_cast<IUnityGraphicsVulkan*>(handler);
    m_instance = m_unity_vulkan->Instance();
    if (m_instance.device == VK_NULL_HANDLE)
        return -1;

    m_widths = {width, width / 2, width / 2};
    m_heights = {height, height / 2, height / 2};
    m_upload_size = 0;
    for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
    {
        m_offsets[i] = m_upload_size;
        m_upload_size +=
            static_cast<VkDeviceSize>(m_widths[i]) * m_heights[i];
        if (!create_image(i, m_widths[i], m_heights[i]))
        {
            destroy();
            return -1;
        }
    }

    for (UploadSlot& slot : m_slots)
    {
        if (!slot.upload.create(m_instance, m_upload_size))
        {
            destroy();
            return -1;
        }
    }
    return 1;
}

bool TextureVulkan::create_image(
    unsigned int index,
    unsigned int width,
    unsigned int height)
{
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(
            m_instance.device,
            &image_info,
            nullptr,
            &m_images[index]) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(
        m_instance.device, m_images[index], &requirements);
    std::uint32_t memory_type = 0;
    if (!find_device_memory(
            m_instance.physicalDevice,
            requirements.memoryTypeBits,
            memory_type))
        return false;

    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(
            m_instance.device,
            &allocation,
            nullptr,
            &m_image_memory[index]) != VK_SUCCESS)
        return false;
    return vkBindImageMemory(
               m_instance.device,
               m_images[index],
               m_image_memory[index],
               0) == VK_SUCCESS;
}

void TextureVulkan::get_resource_pointers(
    void*& y,
    void*& u,
    void*& v)
{
    // CreateExternalTexture expects VkImage* rather than the handle value.
    y = &m_images[0];
    u = &m_images[1];
    v = &m_images[2];
}

void TextureVulkan::register_resource_pointers(
    void* y,
    void* u,
    void* v)
{
    m_unity_texture_handles = {y, u, v};
}

TextureVulkan::UploadSlot* TextureVulkan::acquire_slot(
    const UnityVulkanRecordingState& state)
{
    for (UploadSlot& slot : m_slots)
    {
        if (!slot.used || slot.frame <= state.safeFrameNumber)
            return &slot;
    }
    return nullptr;
}

void TextureVulkan::upload(
    unsigned char* y,
    unsigned char* u,
    unsigned char* v)
{
    if (m_unity_vulkan == nullptr || y == nullptr || u == nullptr ||
        v == nullptr)
        return;

    m_unity_vulkan->EnsureOutsideRenderPass();
    UnityVulkanRecordingState initial_state{};
    if (!m_unity_vulkan->CommandRecordingState(
            &initial_state,
            kUnityVulkanGraphicsQueueAccess_DontCare))
        return;
    UploadSlot* slot = acquire_slot(initial_state);
    if (slot == nullptr)
    {
        LOG("TextureVulkan::upload - all staging slots are in flight");
        return;
    }

    const unsigned char* planes[TEXTURE_COUNT] = {y, u, v};
    auto* destination =
        static_cast<unsigned char*>(slot->upload.mapped());
    for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
    {
        const std::size_t length =
            static_cast<std::size_t>(m_widths[i]) * m_heights[i];
        std::memcpy(destination + m_offsets[i], planes[i], length);
    }

    std::array<UnityVulkanImage, TEXTURE_COUNT> images{};
    for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
    {
        if (m_unity_texture_handles[i] == nullptr)
        {
            LOG("TextureVulkan::upload - Unity texture handle %u is null", i);
            return;
        }
        if (!m_unity_vulkan->AccessTexture(
                m_unity_texture_handles[i],
                UnityVulkanWholeImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                kUnityVulkanResourceAccess_PipelineBarrier,
                &images[i]))
        {
            LOG("TextureVulkan::upload - failed to access texture %u", i);
            return;
        }
    }

    UnityVulkanRecordingState state{};
    if (!m_unity_vulkan->CommandRecordingState(
            &state,
            kUnityVulkanGraphicsQueueAccess_DontCare))
        return;
    for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
    {
        VkBufferImageCopy copy{};
        copy.bufferOffset = m_offsets[i];
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {m_widths[i], m_heights[i], 1};
        vkCmdCopyBufferToImage(
            state.commandBuffer,
            slot->upload.buffer(),
            images[i].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);
    }

    for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
    {
        UnityVulkanImage image{};
        if (!m_unity_vulkan->AccessTexture(
            m_unity_texture_handles[i],
            UnityVulkanWholeImage,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            kUnityVulkanResourceAccess_PipelineBarrier,
            &image))
            LOG("TextureVulkan::upload - failed to transition texture %u", i);
    }
    slot->frame = state.currentFrameNumber;
    slot->used = true;
}

void TextureVulkan::destroy()
{
    if (m_instance.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_instance.device);
    for (UploadSlot& slot : m_slots)
    {
        slot.upload.destroy();
        slot.frame = 0;
        slot.used = false;
    }
    for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
    {
        if (m_instance.device != VK_NULL_HANDLE &&
            m_images[i] != VK_NULL_HANDLE)
            vkDestroyImage(m_instance.device, m_images[i], nullptr);
        if (m_instance.device != VK_NULL_HANDLE &&
            m_image_memory[i] != VK_NULL_HANDLE)
            vkFreeMemory(
                m_instance.device, m_image_memory[i], nullptr);
        m_images[i] = VK_NULL_HANDLE;
        m_image_memory[i] = VK_NULL_HANDLE;
    }
    m_unity_vulkan = nullptr;
    m_instance = {};
    m_unity_texture_handles = {};
    m_upload_size = 0;
}

} // namespace openvolumetric::unity
