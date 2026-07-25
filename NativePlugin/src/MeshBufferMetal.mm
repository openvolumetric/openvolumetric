#include "MeshBufferMetal.h"

#include <Logger.h>

#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>

MeshBufferMetal::MeshBufferMetal()
	: m_index_buffer(nullptr),
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
	if (index_buffer.contents == nullptr || vertex_buffer.contents == nullptr)
	{
		LOG("MeshBufferMetal::init - Unity buffers are not CPU accessible");
		return false;
	}

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
	char* index_data = static_cast<char*>(index_buffer.contents);
	char* vertex_data = static_cast<char*>(vertex_buffer.contents);
	if (index_data == nullptr || vertex_data == nullptr)
		return false;

	std::memset(index_data, 0, index_buffer.length);
	std::memset(vertex_data, 0, vertex_buffer.length);

	const size_t index_count = std::min(mesh->indexes.size(), static_cast<size_t>(m_index_count));
	for (size_t i = 0; i < index_count; ++i)
		*reinterpret_cast<int*>(index_data + i * m_index_stride) = mesh->indexes[i];

	const size_t vertex_count = std::min(mesh->verts.size(), static_cast<size_t>(m_vertex_count));
	for (size_t i = 0; i < vertex_count; ++i)
		std::memcpy(vertex_data + i * m_vertex_stride, &mesh->verts[i], sizeof(Vertex));

	if (index_buffer.storageMode == MTLStorageModeManaged)
		[index_buffer didModifyRange:NSMakeRange(0, index_buffer.length)];
	if (vertex_buffer.storageMode == MTLStorageModeManaged)
		[vertex_buffer didModifyRange:NSMakeRange(0, vertex_buffer.length)];
	return true;
}

void MeshBufferMetal::destroy()
{
	m_index_buffer = nullptr;
	m_vertex_buffer = nullptr;
	m_index_count = 0;
	m_vertex_count = 0;
	m_index_stride = 0;
	m_vertex_stride = 0;
}
