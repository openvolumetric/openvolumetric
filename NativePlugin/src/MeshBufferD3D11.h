#pragma once

#include <d3d11.h>

#include <IMeshBuffer.h>

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
	//
	bool init(void* handler, void* index_buffer_handle, int index_count, void* vertex_buffer_handle, int vertex_count);


	//----------------------------------
	//
	bool update();


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
	int m_index_count;
	int m_index_stride;

	// Vertex information
	ID3D11Buffer* m_vertex_buffer_handle;
	int m_vertex_count;
	int m_vertex_stride;


	float time;



};

