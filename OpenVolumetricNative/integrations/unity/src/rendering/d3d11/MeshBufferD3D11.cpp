#include "MeshBufferD3D11.h"

#include <Logger.h>
#include <Mesh.h>

#include <algorithm>
#include <cstring>

namespace openvolumetric::unity
{

MeshBufferD3D11::MeshBufferD3D11() = default;


MeshBufferD3D11::~MeshBufferD3D11()
{
	destroy();
}

void MeshBufferD3D11::destroy()
{
	// Unity owns the D3D11 buffers; this wrapper has no resources to release.
	m_device = nullptr;
	m_index_buffer_handle = nullptr;
	m_vertex_buffer_handle = nullptr;
	m_index_buffer_size = 0;
	m_vertex_buffer_size = 0;
	m_index_stride = 0;
	m_vertex_stride = 0;
}



bool MeshBufferD3D11::init(
	void* handler,
	void* index_buffer_handle,
	int index_buffer_size,
	void* vertex_buffer_handle,
	int vertex_buffer_size)
{
	if (handler == nullptr || index_buffer_handle == nullptr ||
		vertex_buffer_handle == nullptr || index_buffer_size <= 0 ||
		vertex_buffer_size <= 0)
	{
		LOG("MeshBufferD3D11::init - invalid device, buffer, or capacity");
		return false;
	}

	// Device handler
	m_device = (ID3D11Device*)handler;

	// Index buffer / sizes / strides 
	m_index_buffer_size = index_buffer_size;
	m_index_buffer_handle = (ID3D11Buffer*)index_buffer_handle;
	int index_buffer_size_bytes = get_buffer_size(m_index_buffer_handle);
	m_index_stride = (int)index_buffer_size_bytes / index_buffer_size;

	//vertex buffer / sizes / strides
	m_vertex_buffer_size= vertex_buffer_size;
	m_vertex_buffer_handle = (ID3D11Buffer*)vertex_buffer_handle;
	int vertex_buffer_size_bytes = get_buffer_size(m_vertex_buffer_handle);
	m_vertex_stride = int(vertex_buffer_size_bytes / vertex_buffer_size);
	if (m_index_stride < static_cast<int>(sizeof(int)) ||
		m_vertex_stride < static_cast<int>(sizeof(Vertex)))
	{
		LOG("MeshBufferD3D11::init - invalid buffer strides (%d, %d)",
			m_index_stride, m_vertex_stride);
		destroy();
		return false;
	}

	// Report index Details
	LOG("MeshBufferD3D11::init - index capacity/bytes/stride: %d/%d/%d",
		index_buffer_size, index_buffer_size_bytes, m_index_stride);
	// Report vertex Details
	LOG("MeshBufferD3D11::init - vertex capacity/bytes/stride: %d/%d/%d",
		vertex_buffer_size, vertex_buffer_size_bytes, m_vertex_stride);
	
	return true;
}


// Function to get vertex buffer size
int MeshBufferD3D11::get_buffer_size(void* buffer_handle)
{
	ID3D11Buffer* d3dbuf = static_cast<ID3D11Buffer*>(buffer_handle);
	if (d3dbuf == nullptr)
		return 0;
	D3D11_BUFFER_DESC desc;
	d3dbuf->GetDesc(&desc);

	return (int)desc.ByteWidth;
}


// Function to update the data buffers 
bool MeshBufferD3D11::update(Mesh* mesh)
{
	if (m_device == nullptr || mesh == nullptr ||
		mesh->indexes.size() > static_cast<std::size_t>(m_index_buffer_size) ||
		mesh->verts.size() > static_cast<std::size_t>(m_vertex_buffer_size))
	{
		LOG("MeshBufferD3D11::update - invalid state or mesh exceeds capacity");
		return false;
	}

	ID3D11DeviceContext* ctx = nullptr;
	m_device->GetImmediateContext(&ctx);
	if (ctx == nullptr)
	{
		LOG("MeshBufferD3D11::update - immediate context is unavailable");
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE mapped_vertex{};
	D3D11_MAPPED_SUBRESOURCE mapped_index{};
	HRESULT result = ctx->Map(
		m_vertex_buffer_handle, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_vertex);
	if (FAILED(result) || mapped_vertex.pData == nullptr)
	{
		LOG("MeshBufferD3D11::update - vertex Map failed (0x%08x)", result);
		ctx->Release();
		return false;
	}
	result = ctx->Map(
		m_index_buffer_handle, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_index);
	if (FAILED(result) || mapped_index.pData == nullptr)
	{
		LOG("MeshBufferD3D11::update - index Map failed (0x%08x)", result);
		ctx->Unmap(m_vertex_buffer_handle, 0);
		ctx->Release();
		return false;
	}

	std::memset(mapped_index.pData, 0,
		static_cast<std::size_t>(m_index_buffer_size) * m_index_stride);
	std::memset(mapped_vertex.pData, 0,
		static_cast<std::size_t>(m_vertex_buffer_size) * m_vertex_stride);
	for (std::size_t index = 0; index < mesh->indexes.size(); ++index)
		std::memcpy(
			static_cast<unsigned char*>(mapped_index.pData) + index * m_index_stride,
			&mesh->indexes[index], sizeof(int));
	for (std::size_t index = 0; index < mesh->verts.size(); ++index)
		std::memcpy(
			static_cast<unsigned char*>(mapped_vertex.pData) + index * m_vertex_stride,
			&mesh->verts[index], sizeof(Vertex));

	ctx->Unmap(m_vertex_buffer_handle, 0);
	ctx->Unmap(m_index_buffer_handle, 0);
	ctx->Release();
	return true;
}

} // namespace openvolumetric::unity
