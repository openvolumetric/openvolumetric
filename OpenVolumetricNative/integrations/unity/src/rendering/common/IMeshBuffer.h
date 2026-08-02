#pragma once

#include <Mesh.h>

namespace openvolumetric::unity
{

/// Unity-specific uploader for one engine-neutral decoded mesh.
class IMeshBuffer
{
public:
	virtual ~IMeshBuffer() = default;

	/// Attaches to Unity-owned index and vertex buffers and their capacities.
	virtual bool init(
		void* graphics_interface,
		void* index_handle,
		int index_count,
		void* vertex_handle,
		int vertex_count) = 0;
	/// Uploads a complete mesh on Unity's render thread.
	virtual bool update(Mesh* mesh) = 0;
	/// Releases integration state without releasing Unity-owned buffers.
	virtual void destroy() = 0;
};

} // namespace openvolumetric::unity
