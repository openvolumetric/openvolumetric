#pragma once

#include <ITexture.h>
#include <Unity/IUnityGraphicsVulkan.h>

#include "VulkanUploadBuffer.h"

#include <array>

/// Uploads YUV420P planes into Unity-owned Vulkan images.
class TextureVulkan : public ITexture
{
public:
    /// Constructs an empty image/staging set.
    TextureVulkan();
    /// Releases staging and fallback image resources.
    ~TextureVulkan() override;

    /// Records frame dimensions and obtains Unity's Vulkan interface.
    int init(void* handler, unsigned int width, unsigned int height) override;
    /// Returns fallback image handles before Unity supplies external textures.
    void getResourcePointers(
        void*& y, void*& u, void*& v) override;
    /// Resolves Unity texture handles to Vulkan images used for transfers.
    void registerResourcePointers(
        void* y, void* u, void* v) override;
    /// Copies one YUV frame into a free staging slot and records image barriers
    /// and buffer-to-image transfers on Unity's command buffer.
    void upload(
        unsigned char* y,
        unsigned char* u,
        unsigned char* v) override;
    /// Releases staging buffers and any uploader-owned fallback images.
    void destroy() override;

private:
    static constexpr std::size_t kUploadSlots = 4;

    struct UploadSlot
    {
        VulkanUploadBuffer upload;
        unsigned long long frame = 0;
        bool used = false;
    };

    /// Creates an uploader-owned single-channel fallback image.
    bool create_image(
        unsigned int index,
        unsigned int width,
        unsigned int height);
    /// Selects a staging slot no longer used by an in-flight Unity frame.
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
