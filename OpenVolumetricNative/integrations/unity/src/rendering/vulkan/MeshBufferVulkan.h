#pragma once

#include <IMeshBuffer.h>
#include <Unity/IUnityGraphicsVulkan.h>

#include "VulkanUploadBuffer.h"

#include <array>

namespace openvolumetric::unity
{

/// Uploads decoded meshes into Unity Vulkan buffers using rotating staging slots.
class MeshBufferVulkan : public IMeshBuffer
{
public:
    /// Constructs an unattached uploader.
    MeshBufferVulkan();
    /// Releases all staging buffers.
    ~MeshBufferVulkan() override;

    /// Registers Unity buffer handles and allocates staging slots.
    bool init(
        void* handler,
        void* index_handle,
        int index_count,
        void* vertex_handle,
        int vertex_count) override;
    /// Records index and vertex transfers into Unity's current command buffer.
    bool update(Mesh* mesh) override;
    /// Releases staging allocations and invalidates Unity handles.
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

    /// Selects a slot no longer referenced by an in-flight Unity frame.
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

} // namespace openvolumetric::unity
