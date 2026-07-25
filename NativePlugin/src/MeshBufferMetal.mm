#include "MeshBufferMetal.h"

#include <Logger.h>
#include <Unity/IUnityGraphicsMetal.h>

#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>

MeshBufferMetal::MeshBufferMetal()
	: m_device(nullptr),
	  m_unity_metal(nullptr),
	  m_index_buffer(nullptr),
	  m_vertex_buffer(nullptr),
	  m_index_count(0),
	  m_vertex_count(0),
	  m_index_stride(0),
	  m_vertex_stride(0)
{
}

MeshBufferMetal::~MeshBufferMetal()
{
	destroy();
}

bool MeshBufferMetal::init(
	void* handler,
	void* index_handle,
	int index_count,
	void* vertex_handle,
	int vertex_count)
{
	if (handler == nullptr || index_handle == nullptr || vertex_handle == nullptr ||
		index_count <= 0 || vertex_count <= 0)
		return false;

	id<MTLBuffer> index_buffer = (__bridge id<MTLBuffer>)index_handle;
	id<MTLBuffer> vertex_buffer = (__bridge id<MTLBuffer>)vertex_handle;
	IUnityGraphicsMetal* unity_metal =
		static_cast<IUnityGraphicsMetal*>(handler);
	id<MTLDevice> device = unity_metal->MetalDevice();
	if (device == nil)
		return false;

	m_device = (__bridge void*)device;
	m_unity_metal = unity_metal;
	m_index_buffer = index_handle;
	m_vertex_buffer = vertex_handle;
	m_index_count = index_count;
	m_vertex_count = vertex_count;
	m_index_stride = static_cast<int>(index_buffer.length / index_count);
	m_vertex_stride = static_cast<int>(vertex_buffer.length / vertex_count);

	if (m_index_stride < static_cast<int>(sizeof(int)) ||
		m_vertex_stride < static_cast<int>(sizeof(Vertex)))
	{
		LOG("MeshBufferMetal::init - unexpected buffer layout (%d, %d)",
			m_index_stride, m_vertex_stride);
		destroy();
		return false;
	}
	return true;
}

bool MeshBufferMetal::update(Mesh* mesh)
{
	if (mesh == nullptr || m_index_buffer == nullptr || m_vertex_buffer == nullptr)
		return false;

	id<MTLBuffer> index_buffer = (__bridge id<MTLBuffer>)m_index_buffer;
	id<MTLBuffer> vertex_buffer = (__bridge id<MTLBuffer>)m_vertex_buffer;
	id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
	IUnityGraphicsMetal* unity_metal =
		static_cast<IUnityGraphicsMetal*>(m_unity_metal);
	if (device == nil || unity_metal == nullptr)
		return false;

	id<MTLBuffer> index_staging =
		[device newBufferWithLength:index_buffer.length
		                   options:MTLResourceStorageModeShared];
	id<MTLBuffer> vertex_staging =
		[device newBufferWithLength:vertex_buffer.length
		                   options:MTLResourceStorageModeShared];
	if (index_staging == nil || vertex_staging == nil)
		return false;

	char* index_data = static_cast<char*>(index_staging.contents);
	char* vertex_data = static_cast<char*>(vertex_staging.contents);
	std::memset(index_data, 0, index_staging.length);
	std::memset(vertex_data, 0, vertex_staging.length);

	const size_t index_count = std::min(mesh->indexes.size(), static_cast<size_t>(m_index_count));
	for (size_t i = 0; i < index_count; ++i)
		*reinterpret_cast<int*>(index_data + i * m_index_stride) = mesh->indexes[i];

	const size_t vertex_count = std::min(mesh->verts.size(), static_cast<size_t>(m_vertex_count));
	for (size_t i = 0; i < vertex_count; ++i)
		std::memcpy(vertex_data + i * m_vertex_stride, &mesh->verts[i], sizeof(Vertex));

	unity_metal->EndCurrentCommandEncoder();
	id<MTLCommandBuffer> command_buffer =
		(id<MTLCommandBuffer>)unity_metal->CurrentCommandBuffer();
	if (command_buffer == nil)
		return false;

	id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
	if (blit == nil)
		return false;
	[blit copyFromBuffer:index_staging
	        sourceOffset:0
	            toBuffer:index_buffer
	   destinationOffset:0
	                size:index_buffer.length];
	[blit copyFromBuffer:vertex_staging
	        sourceOffset:0
	            toBuffer:vertex_buffer
	   destinationOffset:0
	                size:vertex_buffer.length];
	[blit endEncoding];
	return true;
}

void MeshBufferMetal::destroy()
{
	m_device = nullptr;
	m_unity_metal = nullptr;
	m_index_buffer = nullptr;
	m_vertex_buffer = nullptr;
	m_index_count = 0;
	m_vertex_count = 0;
	m_index_stride = 0;
	m_vertex_stride = 0;
}
