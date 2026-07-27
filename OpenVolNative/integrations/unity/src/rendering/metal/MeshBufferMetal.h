#pragma once

#include <IMeshBuffer.h>

/// Copies decoded meshes into Unity-owned Metal buffers on the render thread.
class MeshBufferMetal : public IMeshBuffer
{
public:
	/// Constructs an unattached uploader.
	MeshBufferMetal();
	/// Releases retained Metal interface state.
	~MeshBufferMetal() override;

	/// Records Unity's Metal interface and destination buffer capacities.
	bool init(void* handler, void* index_handle, int index_count, void* vertex_handle, int vertex_count) override;
	/// Copies one mesh into the current native Metal buffers.
	bool update(Mesh* mesh) override;
	/// Clears borrowed handles without releasing Unity-owned buffers.
	void destroy() override;

private:
	void* m_device;
	void* m_unity_metal;
	void* m_index_buffer;
	void* m_vertex_buffer;
	int m_index_count;
	int m_vertex_count;
	int m_index_stride;
	int m_vertex_stride;
};
