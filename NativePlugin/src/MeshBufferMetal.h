#pragma once

#include <IMeshBuffer.h>

class MeshBufferMetal : public IMeshBuffer
{
public:
	MeshBufferMetal();
	~MeshBufferMetal() override;

	bool init(void* handler, void* index_handle, int index_count, void* vertex_handle, int vertex_count) override;
	bool update(Mesh* mesh) override;
	void destroy() override;

private:
	void* m_index_buffer;
	void* m_vertex_buffer;
	int m_index_count;
	int m_vertex_count;
	int m_index_stride;
	int m_vertex_stride;
};
