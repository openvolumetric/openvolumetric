#include "MeshBufferD3D11.h"

#include <Logger.h>
#include <Mesh.h>

#include <assert.h>
#include <thread>

//----------------------------------
//
//----------------------------------
namespace openvol::unity
{

MeshBufferD3D11::MeshBufferD3D11()
{
	// Device Pointer
	m_D3D11Device = NULL;

	// index information
	m_index_buffer_handle	= NULL;
	m_index_buffer_size		= -1;
	m_index_stride			= -1;


	// Vertex information
	m_vertex_buffer_handle	= NULL;
	m_vertex_buffer_size	= -1;
	m_vertex_stride			= -1;
}


//----------------------------------
//
//----------------------------------
MeshBufferD3D11::~MeshBufferD3D11()
{

}

//----------------------------------
//
//----------------------------------
void MeshBufferD3D11::destroy()
{
	// Unity owns the D3D11 buffers; this wrapper has no resources to release.
}



//----------------------------------
//
//----------------------------------
bool MeshBufferD3D11::init(void* handler, void* index_buffer_handle, int index_buffer_size, void* vertex_buffer_handle, int vertex_buffer_size)
{
	// Check device handler
	if (handler == NULL) 
	{
		LOG("MeshBufferD311::init - error handler == NULL");
		return false;
	}

	// check index buffer handle
	if (index_buffer_handle == NULL)
	{
		LOG("MeshBufferD311::init - error index_buffer_handle == NULL");
		return false;
	}

	// check vertex buffer handle
	if (vertex_buffer_handle == NULL)
	{
		LOG("MeshBufferD311::init - error vertex_buffer_handle == NULL");
		return false;
	}

	// Device handler
	m_D3D11Device = (ID3D11Device*)handler;

	// Index buffer / sizes / strides 
	m_index_buffer_size = index_buffer_size;
	m_index_buffer_handle = (ID3D11Buffer*)index_buffer_handle;
	assert(m_index_buffer_handle);
	int index_buffer_size_bytes = get_buffer_size(m_index_buffer_handle);
	m_index_stride = (int)index_buffer_size_bytes / index_buffer_size;

	//vertex buffer / sizes / strides
	m_vertex_buffer_size= vertex_buffer_size;
	m_vertex_buffer_handle = (ID3D11Buffer*)vertex_buffer_handle;
	assert(m_vertex_buffer_handle);
	int vertex_buffer_size_bytes = get_buffer_size(m_vertex_buffer_handle);
	m_vertex_stride = int(vertex_buffer_size_bytes / vertex_buffer_size);

	// Report index Details
	LOG("MeshBufferD311::init - index_buffer_handle :      %d", index_buffer_handle);
	LOG("MeshBufferD311::init - index_buffer_size:         %d", index_buffer_size);
	LOG("MeshBufferD311::init - index_buffer_size_bytes:   %d", index_buffer_size_bytes);
	LOG("MeshBufferD311::init - index_stride:              %d", m_index_stride);
	// Report vertex Details
	LOG("MeshBufferD311::init - vertex_buffer_handle :     %d", vertex_buffer_handle);
	LOG("MeshBufferD311::init - vertex_buffer_size:        %d", vertex_buffer_size);
	LOG("MeshBufferD311::init - vertex_buffer_size_bytes:  %d", vertex_buffer_size_bytes);
	LOG("MeshBufferD311::init - vertex_stride:             %d", m_vertex_stride);
	
	// Done
	return true;
}


//----------------------------------
// Function to get vertex buffer size
//----------------------------------
int MeshBufferD3D11::get_buffer_size(void* bufferHandle)
{
	//
	ID3D11Buffer* d3dbuf = (ID3D11Buffer*)bufferHandle;
	assert(d3dbuf);
	D3D11_BUFFER_DESC desc;
	d3dbuf->GetDesc(&desc);

	// 
	return (int)desc.ByteWidth;
}


//----------------------------------
// function to compute buffer stride
//----------------------------------
int MeshBufferD3D11::compute_stride(void* bufferHandle, int count)
{
	//
	ID3D11Buffer* d3dbuf = (ID3D11Buffer*)bufferHandle;
	assert(d3dbuf);
	D3D11_BUFFER_DESC desc;
	d3dbuf->GetDesc(&desc);

	// 
	size_t buffer_size = desc.ByteWidth;

	//
	return  int(buffer_size / count);
}

//----------------------------------
// Function to update the data buffers 
//----------------------------------
bool MeshBufferD3D11::update(Mesh* mesh)
{
	//	LOG("MeshBufferD311::update - start");

	// Get Context
	ID3D11DeviceContext* ctx = NULL;
	this->m_D3D11Device->GetImmediateContext(&ctx);

	//
	D3D11_MAPPED_SUBRESOURCE mapped_vertexbuffer, mapped_indexbuffer;
	ctx->Map(m_vertex_buffer_handle,	0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_vertexbuffer);
	ctx->Map(m_index_buffer_handle,		0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_indexbuffer);

	// Update Index Buffer data
	std::thread index_update_thread = std::thread([&]() 
	{
		//Pointer to data
		char* bufferPtr = (char*)mapped_indexbuffer.pData;
		memset(bufferPtr, 0, m_index_buffer_size * m_index_stride);

		// Update vertex
		for (int i = 0; i < mesh->indexes.size(); i++)
		{
			//Set value
			int& dst = *(int*)bufferPtr;
			dst = mesh->indexes[i];

			// increment buffer
			bufferPtr += m_index_stride;
		}
	});


	// Update Vertex Buffer
	std::thread vertex_update_thread = std::thread([&]() 
	{
		//Pointer to data
		char* bufferPtr = (char*)mapped_vertexbuffer.pData;

		//Zero all data
		memset(bufferPtr, 0, m_vertex_buffer_size * m_vertex_stride);

		// Update vertex buffer data
		for (int i = 0; i < mesh->verts.size(); i++)
		{
			// Get source vertex 
			const Vertex& src = mesh->verts[i];

			// Get dst vertex and set values
			Vertex& dst = *(Vertex*)bufferPtr;
			dst.pos[0]		= src.pos[0];			dst.pos[1]		= src.pos[1];			dst.pos[2]		= src.pos[2];
			dst.normal[0]	= src.normal[0];		dst.normal[1]	= src.normal[1];		dst.normal[2]	= src.normal[2];
			dst.uv[0]		= src.uv[0];			dst.uv[1]		= src.uv[1];

			// Increment buffer by stride value
			bufferPtr += m_vertex_stride;
		}
	});


	// index buffer update thread to join main thread
	if (index_update_thread.joinable())
	{
		index_update_thread.join();
	}

	// index buffer update thread to join main thread
	if (vertex_update_thread.joinable())
	{
		vertex_update_thread.join();
	}

	// Release Context and resouces
	ctx->Unmap(m_vertex_buffer_handle, 0);
	ctx->Unmap(m_index_buffer_handle, 0);
	ctx->Release();

	//
//	LOG("MeshBufferD311::update - end");
	return true;

}

} // namespace openvol::unity
