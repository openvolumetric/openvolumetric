#pragma once

#include <IMeshBuffer.h>
#include <Unity/IUnityGraphicsVulkan.h>

#include "VulkanUploadBuffer.h"

#include <array>

class MeshBufferVulkan : public IMeshBuffer
{
public:
    MeshBufferVulkan();
    ~MeshBufferVulkan() override;

    bool init(
        void* handler,
        void* index_handle,
        int index_count,
        void* vertex_handle,
        int vertex_count) override;
    bool update(Mesh* mesh) override;
    void destroy() override;

private:
    static constexpr std::size_t kUploadSlots = 4;

    struct UploadSlot
    {
        VulkanUploadBuffer index;
        VulkanUploadBuffer vertex;
        unsigned long long frame = 0;
        bool used = false;
    };

    UploadSlot* acquire_slot(
        const UnityVulkanRecordingState& state);

    IUnityGraphicsVulkan* m_unity_vulkan = nullptr;
    UnityVulkanInstance m_instance{};
    void* m_index_handle = nullptr;
    void* m_vertex_handle = nullptr;
    int m_index_count = 0;
    int m_vertex_count = 0;
    std::array<UploadSlot, kUploadSlots> m_slots;
};
