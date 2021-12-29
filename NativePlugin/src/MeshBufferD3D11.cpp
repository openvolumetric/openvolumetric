#include "MeshBufferD3D11.h"

#include <Logger.h>
#include <Mesh.h>

#include <assert.h>
#include <thread>

//----------------------------------
//
//
MeshBufferD3D11::MeshBufferD3D11()
{
	// Device Pointer
	m_D3D11Device = NULL;

	// index information
	m_index_buffer_handle	= NULL;
	m_index_count			= -1;
	m_index_stride			= -1;

	// Vertex information
	m_vertex_buffer_handle	= NULL;
	m_vertex_count			= -1;
	m_vertex_stride			= -1;

	time = 0.0;
}

//----------------------------------
//
//
MeshBufferD3D11::~MeshBufferD3D11()
{

}


//----------------------------------
//
//
bool MeshBufferD3D11::init(void* handler, void* index_buffer_handle, int index_count, void* vertex_buffer_handle, int vertex_count)
{
	//
	if (handler == NULL) 
	{
		LOG("MeshBufferD311::init - error handler == NULL");
		return -1;
	}

	//
	if (index_buffer_handle == NULL)
	{
		LOG("MeshBufferD311::init - error index_buffer_handle == NULL");
		return false;
	}

	//
	if (vertex_buffer_handle == NULL)
	{
		LOG("MeshBufferD311::init - error vertex_buffer_handle == NULL");
		return false;
	}


	//
	m_D3D11Device = (ID3D11Device*)handler;

	//
	m_index_count = index_count;
	m_index_buffer_handle = (ID3D11Buffer*)index_buffer_handle;
	assert(m_index_buffer_handle);
	int index_buffer_size_bytes = get_buffer_size(m_index_buffer_handle);
	m_index_stride = (int)index_buffer_size_bytes / index_count;

	//
	m_vertex_count = vertex_count;
	m_vertex_buffer_handle = (ID3D11Buffer*)vertex_buffer_handle;
	assert(m_vertex_buffer_handle);
	int vertex_buffer_size_bytes = get_buffer_size(m_vertex_buffer_handle);
	m_vertex_stride = int(vertex_buffer_size_bytes / vertex_count);

	// Report Details to console
	LOG("MeshBufferD311::init - index_buffer_handle :      %d", index_buffer_handle);
	LOG("MeshBufferD311::init - index_buffer_size_bytes:   %d", index_buffer_size_bytes);
	LOG("MeshBufferD311::init - index_count:               %d", m_index_count);
	LOG("MeshBufferD311::init - index_stride:              %d", m_index_stride);
	
	LOG("MeshBufferD311::init - vertex_buffer_handle :     %d", vertex_buffer_handle);
	LOG("MeshBufferD311::init - vertex_buffer_size_bytes:  %d", vertex_buffer_size_bytes);
	LOG("MeshBufferD311::init - vertex_count:              %d", m_vertex_count);
	LOG("MeshBufferD311::init - vertex_stride:             %d", m_vertex_stride);
	
	// Done
	return true;
}


//----------------------------------
//
//
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
//
//
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
//
//
bool MeshBufferD3D11::update()
{
//	LOG("MeshBufferD311::update - start");

	// Get Context
	ID3D11DeviceContext* ctx = NULL;
	this->m_D3D11Device->GetImmediateContext(&ctx);

	//
	D3D11_MAPPED_SUBRESOURCE mapped_vertexbuffer, mapped_indexbuffer;
	ctx->Map(m_vertex_buffer_handle, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_vertexbuffer);
	ctx->Map(m_index_buffer_handle, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_indexbuffer);

	// Update Vertex Buffer
	std::thread vertex_update_thread = std::thread([&]() {

		//Pointer to data
		char* bufferPtr = (char*)mapped_vertexbuffer.pData;
		float data[8] = {
			0.0, 0.0,
			1.0, 0.0,
			0.0, 1.0,
			1.0, 1.0
		};

		// Update vertex
		for (int i = 0; i < 4; i++)
		{
			//
			Vertex& dst = *(Vertex*)bufferPtr;
			dst.pos[0] = data[i * 2 + 0];			dst.pos[1] = data[i * 2 + 1];			dst.pos[2] = 0.f;
			dst.normal[0] = 0.f;					dst.normal[1] = 0.f;					dst.normal[2] = -1.f;
			dst.uv[0] = data[i * 2 + 0];			dst.uv[1] = data[i * 2 + 1];
			bufferPtr += m_vertex_stride;
		}
	});

	// Update Vertex Buffer
	std::thread index_update_thread = std::thread([&]() {

		//Pointer to data
		char* bufferPtr_index = (char*)mapped_indexbuffer.pData;
		int data_index[6] = {
			0,	2,	1,
			2,	3,	1
		};

		// Update vertex
		for (int i = 0; i < 6; i++)
		{
			//
			int& dst = *(int*)bufferPtr_index;
			dst = data_index[i];

			bufferPtr_index += m_index_stride;
		}
		});


	//
	if (vertex_update_thread.joinable())
	{
		vertex_update_thread.join();
	}

	//
	if (index_update_thread.joinable())
	{
		index_update_thread.join();
	}

	// Release Context
	ctx->Unmap(m_vertex_buffer_handle, 0);
	ctx->Unmap(m_index_buffer_handle, 0);

	ctx->Release();

	//
//	LOG("MeshBufferD311::update - end");
	return true;
}








