#pragma once
class ITexture
{
public:

	//----------------------------------
	//
	ITexture() {};

	//----------------------------------
	//
	virtual ~ITexture() {};

	//----------------------------------
	//	
	virtual int init(void* handler, unsigned int width, unsigned int height) = 0;

	//----------------------------------
	//
	virtual void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv) = 0;

	//----------------------------------
	// Register the handles Unity returns after wrapping external textures.
	// Backends that can use their original resource pointers need no override.
	virtual void registerResourcePointers(
		void* ptry, void* ptru, void* ptrv) {}
	
	//----------------------------------
	//
	virtual void upload(unsigned char* ych, unsigned char* uch, unsigned char* vch) = 0;

	//----------------------------------
	//
	virtual void destroy() = 0;

	//----------------------------------
	//
	static const unsigned int CPU_ALIGMENT = 64;
	
	//----------------------------------
	//
	static const unsigned int TEXTURE_NUM = 3;
	
};
