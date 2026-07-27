#pragma once

#include <d3d11.h>

#include <IMeshBuffer.h>
#include <Mesh.h>

namespace openvol::unity
{

/// Copies decoded meshes into Unity-owned D3D11 buffers.
class MeshBufferD3D11: public IMeshBuffer
{
public:
	/// Constructs an unattached uploader.
	MeshBufferD3D11();
	
	/// Releases retained D3D11 references.
	~MeshBufferD3D11() override;

	/// Retains the D3D11 device and validates Unity buffer capacity/stride.
	bool init(void* handler, void* index_buffer_handle, int index_buffer_size, void* vertex_buffer_handle, int vertex_buffer_size) override;

	/// Updates both destination buffers with one decoded mesh.
	bool update(Mesh* mesh) override;

	/// Releases retained COM references without destroying Unity resources.
	void destroy() override;

protected:
	/// Returns the D3D11 byte width of a Unity buffer handle.
	int get_buffer_size(void* bufferHandle);

	/// Derives per-element stride from buffer byte width and element count.
	int compute_stride(void* bufferHandle, int count);


private:

	//----------------------------------
	// Device
	//----------------------------------
	ID3D11Device* m_D3D11Device; 

	//----------------------------------
	// index information
	//----------------------------------
	ID3D11Buffer* m_index_buffer_handle;
	int m_index_buffer_size;
	int m_index_stride;

	//----------------------------------
	// Vertex information
	//----------------------------------
	ID3D11Buffer* m_vertex_buffer_handle;
	int m_vertex_buffer_size;
	int m_vertex_stride;

};

} // namespace openvol::unity
