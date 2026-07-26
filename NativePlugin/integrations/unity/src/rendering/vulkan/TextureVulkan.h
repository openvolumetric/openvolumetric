#pragma once

#include <ITexture.h>
#include <Unity/IUnityGraphicsVulkan.h>

#include "VulkanUploadBuffer.h"

#include <array>

class TextureVulkan : public ITexture
{
public:
    TextureVulkan();
    ~TextureVulkan() override;

    int init(void* handler, unsigned int width, unsigned int height) override;
    void getResourcePointers(
        void*& y, void*& u, void*& v) override;
    void registerResourcePointers(
        void* y, void* u, void* v) override;
    void upload(
        unsigned char* y,
        unsigned char* u,
        unsigned char* v) override;
    void destroy() override;

private:
    static constexpr std::size_t kUploadSlots = 4;

    struct UploadSlot
    {
        VulkanUploadBuffer upload;
        unsigned long long frame = 0;
        bool used = false;
    };

    bool create_image(
        unsigned int index,
        unsigned int width,
        unsigned int height);
    UploadSlot* acquire_slot(
        const UnityVulkanRecordingState& state);

    IUnityGraphicsVulkan* m_unity_vulkan = nullptr;
    UnityVulkanInstance m_instance{};
    std::array<VkImage, TEXTURE_NUM> m_images{};
    std::array<void*, TEXTURE_NUM> m_unity_texture_handles{};
    std::array<VkDeviceMemory, TEXTURE_NUM> m_image_memory{};
    std::array<unsigned int, TEXTURE_NUM> m_widths{};
    std::array<unsigned int, TEXTURE_NUM> m_heights{};
    std::array<VkDeviceSize, TEXTURE_NUM> m_offsets{};
    std::array<UploadSlot, kUploadSlots> m_slots;
    VkDeviceSize m_upload_size = 0;
};
