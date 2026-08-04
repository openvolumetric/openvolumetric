#include "MeshBufferD3D12.h"
#include <Logger.h>
#include <algorithm>
#include <cstring>

namespace openvolumetric::unity
{
MeshBufferD3D12::~MeshBufferD3D12() { destroy(); }

bool MeshBufferD3D12::create_upload(
	Microsoft::WRL::ComPtr<ID3D12Resource>& resource, UINT64 size)
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	heap.CreationNodeMask = 1;
	heap.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC description{};
	description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	description.Width = size;
	description.Height = 1;
	description.DepthOrArraySize = 1;
	description.MipLevels = 1;
	description.SampleDesc.Count = 1;
	description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	return SUCCEEDED(m_context->device->CreateCommittedResource(
		&heap, D3D12_HEAP_FLAG_NONE, &description,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)));
}

bool MeshBufferD3D12::init(void* handler, void* index_handle,
	int index_count, void* vertex_handle, int vertex_count)
{
	destroy();
	m_context = static_cast<D3D12UnityContext*>(handler);
	m_index = static_cast<ID3D12Resource*>(index_handle);
	m_vertex = static_cast<ID3D12Resource*>(vertex_handle);
	if (m_context == nullptr || m_context->device == nullptr ||
		m_context->unity == nullptr || m_index == nullptr || m_vertex == nullptr ||
		index_count <= 0 || vertex_count <= 0)
		return false;
	m_index_count = index_count;
	m_vertex_count = vertex_count;
	m_index_bytes = m_index->GetDesc().Width;
	m_vertex_bytes = m_vertex->GetDesc().Width;
	m_index_stride = m_index_bytes / static_cast<UINT64>(index_count);
	m_vertex_stride = m_vertex_bytes / static_cast<UINT64>(vertex_count);
	if (m_index_stride < sizeof(int) || m_vertex_stride < sizeof(Vertex))
		return false;
	for (auto& slot : m_uploads)
		if (!create_upload(slot.index, m_index_bytes) ||
			!create_upload(slot.vertex, m_vertex_bytes))
		{
			destroy();
			return false;
		}
	return true;
}

bool MeshBufferD3D12::update(Mesh* mesh)
{
	if (m_context == nullptr || mesh == nullptr ||
		mesh->indexes.size() > static_cast<std::size_t>(m_index_count) ||
		mesh->verts.size() > static_cast<std::size_t>(m_vertex_count))
		return false;
	UnityGraphicsD3D12RecordingState recording{};
	if (!m_context->unity->CommandRecordingState(&recording) || recording.commandList == nullptr)
		return false;
	ID3D12Fence* frame_fence = m_context->unity->GetFrameFence();
	const UINT64 completed = frame_fence == nullptr ? UINT64_MAX : frame_fence->GetCompletedValue();
	UploadSlot& slot = m_uploads[m_next_slot];
	if (slot.fence > completed)
	{
		LOG("MeshBufferD3D12::update - GPU upload ring is full; frame dropped");
		return false;
	}
	void* index_data = nullptr;
	void* vertex_data = nullptr;
	HRESULT result = slot.index->Map(0, nullptr, &index_data);
	if (FAILED(result) || index_data == nullptr)
	{
		LOG("MeshBufferD3D12::update - index Map failed (0x%08x)", result);
		return false;
	}
	result = slot.vertex->Map(0, nullptr, &vertex_data);
	if (FAILED(result) || vertex_data == nullptr)
	{
		LOG("MeshBufferD3D12::update - vertex Map failed (0x%08x)", result);
		slot.index->Unmap(0, nullptr);
		return false;
	}
	std::memset(index_data, 0, static_cast<std::size_t>(m_index_bytes));
	std::memset(vertex_data, 0, static_cast<std::size_t>(m_vertex_bytes));
	for (std::size_t index = 0; index < mesh->indexes.size(); ++index)
		std::memcpy(static_cast<unsigned char*>(index_data) + index * m_index_stride,
			&mesh->indexes[index], sizeof(int));
	for (std::size_t index = 0; index < mesh->verts.size(); ++index)
		std::memcpy(static_cast<unsigned char*>(vertex_data) + index * m_vertex_stride,
			&mesh->verts[index], sizeof(Vertex));
	slot.index->Unmap(0, nullptr);
	slot.vertex->Unmap(0, nullptr);

	m_context->unity->RequestResourceState(m_index, D3D12_RESOURCE_STATE_COPY_DEST);
	m_context->unity->RequestResourceState(m_vertex, D3D12_RESOURCE_STATE_COPY_DEST);
	recording.commandList->CopyBufferRegion(m_index, 0, slot.index.Get(), 0, m_index_bytes);
	recording.commandList->CopyBufferRegion(m_vertex, 0, slot.vertex.Get(), 0, m_vertex_bytes);
	m_context->unity->NotifyResourceState(m_index, D3D12_RESOURCE_STATE_COPY_DEST, false);
	m_context->unity->NotifyResourceState(m_vertex, D3D12_RESOURCE_STATE_COPY_DEST, false);
	m_context->unity->RequestResourceState(m_index, D3D12_RESOURCE_STATE_INDEX_BUFFER);
	m_context->unity->RequestResourceState(m_vertex, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	slot.fence = m_context->unity->GetNextFrameFenceValue();
	m_next_slot = (m_next_slot + 1) % kUploadSlotCount;
	return true;
}

void MeshBufferD3D12::destroy()
{
	for (auto& slot : m_uploads)
	{
		slot.index.Reset();
		slot.vertex.Reset();
		slot.fence = 0;
	}
	m_context = nullptr;
	m_index = nullptr;
	m_vertex = nullptr;
	m_index_bytes = m_vertex_bytes = 0;
	m_index_count = m_vertex_count = 0;
	m_index_stride = m_vertex_stride = 0;
	m_next_slot = 0;
}
}
