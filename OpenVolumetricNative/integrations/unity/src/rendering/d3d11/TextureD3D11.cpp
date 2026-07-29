#include "TextureD3D11.h"

#include <Logger.h>
#include <thread>

namespace openvolumetric::unity
{

TextureD3D11::TextureD3D11() = default;

TextureD3D11::~TextureD3D11() = default;

void TextureD3D11::destroy()
{
	LOG("TextureD3D11::destroy - start");

	m_device = nullptr;
	m_width_y = m_height_y = m_length_y = 0;
	m_width_uv = m_height_uv = m_length_uv = 0;

	for (int i = 0; i < TEXTURE_NUM; i++) {
		if (m_textures[i] != nullptr) {
			m_textures[i]->Release();
			m_textures[i] = nullptr;
		}

		if (m_shader_resource_views[i] != nullptr) {
			m_shader_resource_views[i]->Release();
			m_shader_resource_views[i] = nullptr;
		}
	}
	LOG("TextureD3D11::destroy - end");
}



int TextureD3D11::init(void* handler, unsigned int width, unsigned int height)
{
	// Check handler
	if (handler == nullptr) {
		return -1;
	}

	m_device = (ID3D11Device*)handler;
	
	m_width_y = (unsigned int)(ceil((float)width / CPU_ALIGMENT) * CPU_ALIGMENT);
	m_height_y = height;
	m_length_y = m_width_y * m_height_y;

	m_width_uv = m_width_y / 2;
	m_height_uv = m_height_y / 2;
	m_length_uv = m_width_uv * m_height_uv;

	//	For YUV420
	//	Y channel
	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D11_TEXTURE2D_DESC));
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DYNAMIC;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	texDesc.MiscFlags = 0;

	HRESULT result = m_device->CreateTexture2D(&texDesc, nullptr, (ID3D11Texture2D**)(&(m_textures[0])));
	if (FAILED(result)) 
	{
		LOG("TextureD3D11::create - Create texture Y fail. Error code: %x", result);
		return -1;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	shaderResourceViewDesc.Format = DXGI_FORMAT_A8_UNORM;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	result = m_device->CreateShaderResourceView((ID3D11Texture2D*)(m_textures[0]), &shaderResourceViewDesc, &(m_shader_resource_views[0]));
	if (FAILED(result)) {
		LOG("TextureD3D11::create - Create shader resource view Y fail. Error code: %x", result);
		return -1;
	}

	//	UV channel
	texDesc.Width = width / 2;
	texDesc.Height = height / 2;
	result = m_device->CreateTexture2D(&texDesc, nullptr, (ID3D11Texture2D**)(&(m_textures[1])));
	if (FAILED(result)) {
		LOG("Create texture U fail. Error code: %x", result);
	}

	result = m_device->CreateShaderResourceView((ID3D11Texture2D*)(m_textures[1]), &shaderResourceViewDesc, &(m_shader_resource_views[1]));
	if (FAILED(result)) {
		LOG("TextureD3D11::create - Create shader resource view U fail. Error code: %x", result);
		return -1;
	}

	result = m_device->CreateTexture2D(&texDesc, nullptr, (ID3D11Texture2D**)(&(m_textures[2])));
	if (FAILED(result)) {
		LOG("TextureD3D11::create - Create texture V fail. Error code: %x", result);
		return -1;
	}

	result = m_device->CreateShaderResourceView((ID3D11Texture2D*)(m_textures[2]), &shaderResourceViewDesc, &(m_shader_resource_views[2]));
	if (FAILED(result)) {
		LOG("TextureD3D11::create - Create shader resource view V fail. %x", result);
		return -1;
	}

	return 1;
}

void TextureD3D11::getResourcePointers(void*& ptry, void*& ptru, void*& ptrv)
{
	if (m_device == nullptr)
	{
		return;
	}

	ptry = m_shader_resource_views[0];
	ptru = m_shader_resource_views[1];
	ptrv = m_shader_resource_views[2];
}

void TextureD3D11::upload(unsigned char* ych, unsigned char* uch, unsigned char* vch)
{
	if (m_device == nullptr)
	{
		return;
	}

	ID3D11DeviceContext* ctx = nullptr;
	m_device->GetImmediateContext(&ctx);

	D3D11_MAPPED_SUBRESOURCE mappedResource[TEXTURE_NUM];
	for (int i = 0; i < TEXTURE_NUM; i++) {
		ZeroMemory(&(mappedResource[i]), sizeof(D3D11_MAPPED_SUBRESOURCE));
		ctx->Map(m_textures[i], 0, D3D11_MAP_WRITE_DISCARD, 0, &(mappedResource[i]));
	}

	//	Consider padding.
	UINT rowPitchY = mappedResource[0].RowPitch;
	UINT rowPitchUV = mappedResource[1].RowPitch;

	uint8_t* ptrMappedY = (uint8_t*)(mappedResource[0].pData);
	uint8_t* ptrMappedU = (uint8_t*)(mappedResource[1].pData);
	uint8_t* ptrMappedV = (uint8_t*)(mappedResource[2].pData);

	//	Two thread memory copy
	std::thread YThread = std::thread([&]() {
		//	Map region has its own row pitch which may different to texture width.
		if (m_width_y == rowPitchY) {
			memcpy(ptrMappedY, ych, m_length_y);
		}
		else {
			//	Handle rowpitch of mapped memory.
			uint8_t* end = ych + m_length_y;
			while (ych != end) {
				memcpy(ptrMappedY, ych, m_width_y);
				ych += m_width_y;
				ptrMappedY += rowPitchY;
			}
		}
		});

	std::thread UVThread = std::thread([&]() {
		if (m_width_uv == rowPitchUV) {
			memcpy(ptrMappedU, uch, m_length_uv);
			memcpy(ptrMappedV, vch, m_length_uv);
		}
		else {
			//	Handle rowpitch of mapped memory.
			//	YUV420, length U == length V
			uint8_t* endU = uch + m_length_uv;
			while (uch != endU) {
				memcpy(ptrMappedU, uch, m_width_uv);
				memcpy(ptrMappedV, vch, m_width_uv);
				uch += m_width_uv;
				vch += m_width_uv;
				ptrMappedU += rowPitchUV;
				ptrMappedV += rowPitchUV;
			}
		}
		});

	if (YThread.joinable()) {
		YThread.join();
	}
	if (UVThread.joinable()) {
		UVThread.join();
	}

	for (int i = 0; i < TEXTURE_NUM; i++) {
		ctx->Unmap(m_textures[i], 0);
	}
	ctx->Release();
}

} // namespace openvolumetric::unity
