#pragma once

#include <d3d11.h>

#include <IMeshBuffer.h>
#include <Mesh.h>


class MeshBufferD3D11: public IMeshBuffer
{

public:

	//----------------------------------
	//
	MeshBufferD3D11();
	
	
	//----------------------------------
	//
	virtual ~MeshBufferD3D11();


	//----------------------------------
	//sdfs
	bool init(void* handler, void* index_buffer_handle, int index_buffer_size, void* vertex_buffer_handle, int vertex_buffer_size);


	//----------------------------------
	//
	bool update(Mesh* mesh);


protected:
	
	//----------------------------------
	//
	//
	int get_buffer_size(void* bufferHandle);

	//----------------------------------
	//
	//
	int compute_stride(void* bufferHandle, int count);


private:

	//
	ID3D11Device* m_D3D11Device; 

	// index information
	ID3D11Buffer* m_index_buffer_handle;
	int m_index_buffer_size;
	int m_index_count;
	int m_index_stride;


	// Vertex information
	ID3D11Buffer* m_vertex_buffer_handle;
	int m_vertex_buffer_size;
	int m_vertex_count;
	int m_vertex_stride;
	int m_vertex_buffer_used;

};

