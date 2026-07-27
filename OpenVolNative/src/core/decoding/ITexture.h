#pragma once
/// Engine-specific uploader for the decoder's planar Y, U, and V images.
///
/// Implementations either expose native resources for Unity to wrap or accept
/// Unity-provided external texture handles, depending on the graphics API.
class ITexture
{
public:
	/// Constructs an unattached texture uploader.
	ITexture() {};

	/// Releases backend state through a base pointer.
	virtual ~ITexture() {};

	/// Allocates or attaches the three texture planes for a luma-sized frame.
	virtual int init(void* handler, unsigned int width, unsigned int height) = 0;

	/// Returns backend resource handles that the engine can wrap.
	virtual void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv) = 0;

	/// Registers handles returned after the engine creates external textures.
	///
	/// Vulkan requires this second handshake because Unity owns the images.
	/// Backends that expose their own resources intentionally use this no-op.
	virtual void registerResourcePointers(
		void* ptry, void* ptru, void* ptrv) {}
	
	/// Copies one decoded YUV420P frame into the three GPU texture planes.
	virtual void upload(unsigned char* ych, unsigned char* uch, unsigned char* vch) = 0;

	/// Releases uploader-owned resources and invalidates registered handles.
	virtual void destroy() = 0;

	/// Alignment used by CPU-visible staging allocations.
	static const unsigned int CPU_ALIGMENT = 64;
	
	/// Number of planes in the supported YUV420P layout.
	static const unsigned int TEXTURE_NUM = 3;
};
