#include "TextureD3D12.h"
#include <Logger.h>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace openvolumetric::unity
{
namespace
{
D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type)
{
	D3D12_HEAP_PROPERTIES properties{};
	properties.Type = type;
	properties.CreationNodeMask = 1;
	properties.VisibleNodeMask = 1;
	return properties;
}
}

TextureD3D12::~TextureD3D12() { destroy(); }

bool TextureD3D12::create_plane(Plane& plane, unsigned int width,
	unsigned int height, unsigned int source_stride)
{
	plane.width = width;
	plane.height = height;
	plane.source_stride = source_stride;
	plane.row_pitch = (width + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
		~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
	D3D12_RESOURCE_DESC description{};
	description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	description.Width = width;
	description.Height = height;
	description.DepthOrArraySize = 1;
	description.MipLevels = 1;
	description.Format = DXGI_FORMAT_A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	HRESULT result = m_context->device->CreateCommittedResource(
		&default_heap, D3D12_HEAP_FLAG_NONE, &description,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
		IID_PPV_ARGS(&plane.texture));
	if (FAILED(result))
	{
		LOG("TextureD3D12::create_plane - texture creation failed (0x%08x)", result);
		return false;
	}

	description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	description.Width = static_cast<UINT64>(plane.row_pitch) * height;
	description.Height = 1;
	description.Format = DXGI_FORMAT_UNKNOWN;
	description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
	for (auto& slot : plane.uploads)
	{
		result = m_context->device->CreateCommittedResource(
			&upload_heap, D3D12_HEAP_FLAG_NONE, &description,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&slot.resource));
		if (FAILED(result))
		{
			LOG("TextureD3D12::create_plane - upload creation failed (0x%08x)", result);
			return false;
		}
	}
	return true;
}

int TextureD3D12::init(void* handler, unsigned int width, unsigned int height)
{
	destroy();
	m_context = static_cast<D3D12UnityContext*>(handler);
	if (m_context == nullptr || m_context->device == nullptr ||
		m_context->unity == nullptr || width < 2 || height < 2)
		return -1;
	const unsigned int aligned_width =
		(width + CPU_ALIGNMENT - 1) & ~(CPU_ALIGNMENT - 1);
	if (!create_plane(m_planes[0], width, height, aligned_width) ||
		!create_plane(m_planes[1], width / 2, height / 2, aligned_width / 2) ||
		!create_plane(m_planes[2], width / 2, height / 2, aligned_width / 2))
	{
		LOG("TextureD3D12::init - failed to allocate texture resources");
		destroy();
		return -1;
	}
	return 1;
}

void TextureD3D12::get_resource_pointers(void*& y, void*& u, void*& v)
{
	y = m_planes[0].texture.Get();
	u = m_planes[1].texture.Get();
	v = m_planes[2].texture.Get();
}

void TextureD3D12::upload(unsigned char* y, unsigned char* u, unsigned char* v)
{
	if (m_context == nullptr || y == nullptr || u == nullptr || v == nullptr)
		return;
	UnityGraphicsD3D12RecordingState recording{};
	if (!m_context->unity->CommandRecordingState(&recording) || recording.commandList == nullptr)
	{
		LOG("TextureD3D12::upload - Unity command list is unavailable");
		return;
	}
	ID3D12Fence* frame_fence = m_context->unity->GetFrameFence();
	const UINT64 completed = frame_fence == nullptr ? UINT64_MAX : frame_fence->GetCompletedValue();
	const UINT64 pending = m_context->unity->GetNextFrameFenceValue();
	unsigned char* sources[TEXTURE_COUNT]{y, u, v};
	for (unsigned int index = 0; index < TEXTURE_COUNT; ++index)
	{
		Plane& plane = m_planes[index];
		UploadSlot& slot = plane.uploads[plane.next_slot];
		if (slot.fence > completed)
		{
			LOG("TextureD3D12::upload - GPU upload ring is full; frame dropped");
			return;
		}
		void* mapped = nullptr;
		const HRESULT map_result = slot.resource->Map(0, nullptr, &mapped);
		if (FAILED(map_result) || mapped == nullptr)
		{
			LOG("TextureD3D12::upload - plane %u Map failed (0x%08x)",
				index, map_result);
			return;
		}
		for (unsigned int row = 0; row < plane.height; ++row)
			std::memcpy(static_cast<unsigned char*>(mapped) + row * plane.row_pitch,
				sources[index] + row * plane.source_stride, plane.width);
		slot.resource->Unmap(0, nullptr);

		m_context->unity->RequestResourceState(
			plane.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = plane.texture.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = slot.resource.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_A8_UNORM;
		source.PlacedFootprint.Footprint.Width = plane.width;
		source.PlacedFootprint.Footprint.Height = plane.height;
		source.PlacedFootprint.Footprint.Depth = 1;
		source.PlacedFootprint.Footprint.RowPitch = plane.row_pitch;
		recording.commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
		m_context->unity->NotifyResourceState(
			plane.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, false);
		m_context->unity->RequestResourceState(
			plane.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		slot.fence = pending;
		plane.next_slot = (plane.next_slot + 1) % kUploadSlotCount;
	}
}

void TextureD3D12::destroy()
{
	for (auto& plane : m_planes)
	{
		plane.texture.Reset();
		for (auto& slot : plane.uploads)
		{
			slot.resource.Reset();
			slot.fence = 0;
		}
		plane = {};
	}
	m_context = nullptr;
}
}
