#pragma once

namespace openvolumetric::unity
{

/// Unity-specific destination for decoded planar Y, U, and V images.
///
/// Implementations expose or register resources using Unity's graphics plug-in
/// conventions. This contract therefore belongs to the Unity integration and
/// is not part of the engine-neutral core presentation boundary.
class ITexture
{
public:
	virtual ~ITexture() = default;

	/// Allocates or attaches three planes for a luma-sized YUV420P frame.
	virtual int init(void* graphics_interface, unsigned int width,
		unsigned int height) = 0;
	/// Returns backend resource handles for Unity to wrap.
	virtual void get_resource_pointers(void*& y, void*& u, void*& v) = 0;
	/// Registers Unity handles after external texture construction when needed.
	virtual void register_resource_pointers(void*, void*, void*) {}
	/// Uploads one complete decoded YUV420P presentation.
	virtual void upload(unsigned char* y, unsigned char* u, unsigned char* v) = 0;
	/// Releases integration-owned resources but never Unity-owned resources.
	virtual void destroy() = 0;

	static constexpr unsigned int CPU_ALIGNMENT = 64;
	static constexpr unsigned int TEXTURE_COUNT = 3;
};

} // namespace openvolumetric::unity
