#pragma once

#include <d3d11.h>

#include <IMeshBuffer.h>
#include <Mesh.h>

namespace openvolumetric::unity
{

/// Copies decoded meshes into Unity-owned D3D11 buffers.
class MeshBufferD3D11 : public IMeshBuffer
{
public:
	/// Constructs an unattached uploader.
	MeshBufferD3D11();
	
	/// Releases retained D3D11 references.
	~MeshBufferD3D11() override;

	/// Retains the D3D11 device and validates Unity buffer capacity/stride.
	bool init(
		void* handler,
		void* index_buffer_handle,
		int index_buffer_size,
		void* vertex_buffer_handle,
		int vertex_buffer_size) override;

	/// Updates both destination buffers with one decoded mesh.
	bool update(Mesh* mesh) override;

	/// Releases retained COM references without destroying Unity resources.
	void destroy() override;

protected:
	/// Returns the D3D11 byte width of a Unity buffer handle.
	int get_buffer_size(void* buffer_handle);

private:
	/// Borrowed device and Unity-owned buffers. Unity guarantees their lifetime
	/// from registration until plugin teardown.
	ID3D11Device* m_device = nullptr;
	ID3D11Buffer* m_index_buffer_handle = nullptr;
	ID3D11Buffer* m_vertex_buffer_handle = nullptr;

	/// Element capacities supplied by the managed mesh allocation.
	int m_index_buffer_size = 0;
	int m_vertex_buffer_size = 0;

	/// Byte distance between adjacent elements in the Unity buffers.
	int m_index_stride = 0;
	int m_vertex_stride = 0;
};

} // namespace openvolumetric::unity
