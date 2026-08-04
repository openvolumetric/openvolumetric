#include "MeshBufferVulkan.h"

#include <Logger.h>

#include <algorithm>
#include <cstring>

namespace openvolumetric::unity
{

MeshBufferVulkan::MeshBufferVulkan() = default;

MeshBufferVulkan::~MeshBufferVulkan()
{
    destroy();
}


bool MeshBufferVulkan::init(
    void* handler,
    void* index_handle,
    int index_count,
    void* vertex_handle,
    int vertex_count)
{
    destroy();
    if (handler == nullptr || index_handle == nullptr ||
        vertex_handle == nullptr || index_count <= 0 || vertex_count <= 0)
        return false;

    m_unity_vulkan = static_cast<IUnityGraphicsVulkan*>(handler);
    m_instance = m_unity_vulkan->Instance();
    if (m_instance.device == VK_NULL_HANDLE)
        return false;

    m_index_handle = index_handle;
    m_vertex_handle = vertex_handle;
    m_index_count = index_count;
    m_vertex_count = vertex_count;
    const VkDeviceSize index_size =
        static_cast<VkDeviceSize>(index_count) * sizeof(std::uint32_t);
    const VkDeviceSize vertex_size =
        static_cast<VkDeviceSize>(vertex_count) * sizeof(Vertex);
    for (UploadSlot& slot : m_slots)
    {
        if (!slot.index.create(m_instance, index_size) ||
            !slot.vertex.create(m_instance, vertex_size))
        {
            destroy();
            return false;
        }
    }
    return true;
}

MeshBufferVulkan::UploadSlot* MeshBufferVulkan::acquire_slot(
    const UnityVulkanRecordingState& state)
{
    for (UploadSlot& slot : m_slots)
    {
        if (!slot.used || slot.frame <= state.safeFrameNumber)
            return &slot;
    }
    return nullptr;
}

bool MeshBufferVulkan::update(Mesh* mesh)
{
    if (mesh == nullptr || m_unity_vulkan == nullptr)
        return false;

    m_unity_vulkan->EnsureOutsideRenderPass();
    UnityVulkanRecordingState initial_state{};
    if (!m_unity_vulkan->CommandRecordingState(
            &initial_state,
            kUnityVulkanGraphicsQueueAccess_DontCare))
        return false;
    UploadSlot* slot = acquire_slot(initial_state);
    if (slot == nullptr)
    {
        LOG("MeshBufferVulkan::update - all staging slots are in flight");
        return false;
    }

    std::memset(
        slot->index.mapped(),
        0,
        static_cast<std::size_t>(slot->index.size()));
    std::memset(
        slot->vertex.mapped(),
        0,
        static_cast<std::size_t>(slot->vertex.size()));
    const std::size_t index_count = std::min(
        mesh->indexes.size(),
        static_cast<std::size_t>(m_index_count));
    const std::size_t vertex_count = std::min(
        mesh->verts.size(),
        static_cast<std::size_t>(m_vertex_count));
    std::memcpy(
        slot->index.mapped(),
        mesh->indexes.data(),
        index_count * sizeof(std::uint32_t));
    std::memcpy(
        slot->vertex.mapped(),
        mesh->verts.data(),
        vertex_count * sizeof(Vertex));

    UnityVulkanBuffer index_buffer{};
    UnityVulkanBuffer vertex_buffer{};
    if (!m_unity_vulkan->AccessBuffer(
            m_index_handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            kUnityVulkanResourceAccess_PipelineBarrier,
            &index_buffer) ||
        !m_unity_vulkan->AccessBuffer(
            m_vertex_handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            kUnityVulkanResourceAccess_PipelineBarrier,
            &vertex_buffer))
        return false;

    if (index_buffer.sizeInBytes > slot->index.size() ||
        vertex_buffer.sizeInBytes > slot->vertex.size())
    {
        LOG("MeshBufferVulkan::update - Unity buffer size changed");
        return false;
    }

    UnityVulkanRecordingState state{};
    if (!m_unity_vulkan->CommandRecordingState(
            &state,
            kUnityVulkanGraphicsQueueAccess_DontCare))
        return false;

    VkBufferCopy index_copy{};
    index_copy.size = index_buffer.sizeInBytes;
    vkCmdCopyBuffer(
        state.commandBuffer,
        slot->index.buffer(),
        index_buffer.buffer,
        1,
        &index_copy);
    VkBufferCopy vertex_copy{};
    vertex_copy.size = vertex_buffer.sizeInBytes;
    vkCmdCopyBuffer(
        state.commandBuffer,
        slot->vertex.buffer(),
        vertex_buffer.buffer,
        1,
        &vertex_copy);

    UnityVulkanBuffer transitioned{};
    const bool index_transitioned = m_unity_vulkan->AccessBuffer(
        m_index_handle,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_ACCESS_INDEX_READ_BIT,
        kUnityVulkanResourceAccess_PipelineBarrier,
        &transitioned);
    const bool vertex_transitioned = m_unity_vulkan->AccessBuffer(
        m_vertex_handle,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        kUnityVulkanResourceAccess_PipelineBarrier,
        &transitioned);
    if (!index_transitioned || !vertex_transitioned)
    {
        LOG("MeshBufferVulkan::update - failed to restore buffer access");
        return false;
    }

    slot->frame = state.currentFrameNumber;
    slot->used = true;
    return true;
}

void MeshBufferVulkan::destroy()
{
    if (m_instance.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_instance.device);
    for (UploadSlot& slot : m_slots)
    {
        slot.index.destroy();
        slot.vertex.destroy();
        slot.frame = 0;
        slot.used = false;
    }
    m_unity_vulkan = nullptr;
    m_instance = {};
    m_index_handle = nullptr;
    m_vertex_handle = nullptr;
    m_index_count = 0;
    m_vertex_count = 0;
}

} // namespace openvolumetric::unity
