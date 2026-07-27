#pragma once


#include <Mesh.h>

/// Engine-specific destination for decoded mesh data.
///
/// Implementations translate the engine-neutral Mesh into graphics resources
/// supplied by an engine integration and run on its render thread.
class IMeshBuffer
{
public:
	/// Constructs an unattached mesh uploader.
	IMeshBuffer() {};

	/// Releases backend-specific state through a base pointer.
	virtual ~IMeshBuffer() {};

	/// Attaches to engine-owned index and vertex buffers.
	///
	/// handler is an optional device/context pointer. Counts describe the
	/// maximum element capacity of the supplied buffers.
	virtual bool init(
		void* handler,
		void* index_handle,
		int index_count,
		void* vertex_handle,
		int vertex_count) = 0;

	/// Uploads one decoded mesh, returning false if it exceeds buffer capacity
	/// or if the backend copy operation fails.
	virtual bool update(Mesh* mesh) = 0;

	/// Releases backend views and synchronization objects, never engine-owned
	/// buffers themselves.
	virtual void destroy() = 0;
};
