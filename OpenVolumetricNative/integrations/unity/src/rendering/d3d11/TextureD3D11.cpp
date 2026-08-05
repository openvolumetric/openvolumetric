#include "TextureD3D11.h"

#include <Logger.h>
#include <cstring>

namespace openvolumetric::unity
{

TextureD3D11::TextureD3D11() = default;

TextureD3D11::~TextureD3D11()
{
	destroy();
}

void TextureD3D11::destroy()
{
	LOG("TextureD3D11::destroy - start");

	m_device = nullptr;
	m_width_y = m_height_y = m_length_y = 0;
	m_width_uv = m_height_uv = m_length_uv = 0;

	for (int i = 0; i < TEXTURE_COUNT; i++) {
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
	destroy();
	if (handler == nullptr || width == 0 || height == 0) {
		LOG("TextureD3D11::init - invalid device or dimensions");
		return -1;
	}

	m_device = (ID3D11Device*)handler;
	
	m_width_y = (unsigned int)(ceil((float)width / CPU_ALIGNMENT) * CPU_ALIGNMENT);
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
		LOG("TextureD3D11::init - CreateTexture2D(Y) failed (0x%08x)", result);
		destroy();
		return -1;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
	shaderResourceViewDesc.Format = DXGI_FORMAT_A8_UNORM;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	result = m_device->CreateShaderResourceView((ID3D11Texture2D*)(m_textures[0]), &shaderResourceViewDesc, &(m_shader_resource_views[0]));
	if (FAILED(result)) {
		LOG("TextureD3D11::init - CreateShaderResourceView(Y) failed (0x%08x)", result);
		destroy();
		return -1;
	}

	//	UV channel
	texDesc.Width = width / 2;
	texDesc.Height = height / 2;
	result = m_device->CreateTexture2D(&texDesc, nullptr, (ID3D11Texture2D**)(&(m_textures[1])));
	if (FAILED(result)) {
		LOG("TextureD3D11::init - CreateTexture2D(U) failed (0x%08x)", result);
		destroy();
		return -1;
	}

	result = m_device->CreateShaderResourceView((ID3D11Texture2D*)(m_textures[1]), &shaderResourceViewDesc, &(m_shader_resource_views[1]));
	if (FAILED(result)) {
		LOG("TextureD3D11::init - CreateShaderResourceView(U) failed (0x%08x)", result);
		destroy();
		return -1;
	}

	result = m_device->CreateTexture2D(&texDesc, nullptr, (ID3D11Texture2D**)(&(m_textures[2])));
	if (FAILED(result)) {
		LOG("TextureD3D11::init - CreateTexture2D(V) failed (0x%08x)", result);
		destroy();
		return -1;
	}

	result = m_device->CreateShaderResourceView((ID3D11Texture2D*)(m_textures[2]), &shaderResourceViewDesc, &(m_shader_resource_views[2]));
	if (FAILED(result)) {
		LOG("TextureD3D11::init - CreateShaderResourceView(V) failed (0x%08x)", result);
		destroy();
		return -1;
	}

	return 1;
}

void TextureD3D11::get_resource_pointers(void*& ptry, void*& ptru, void*& ptrv)
{
	ptry = nullptr;
	ptru = nullptr;
	ptrv = nullptr;
	if (m_device == nullptr)
	{
		LOG("TextureD3D11::get_resource_pointers - texture is not initialized");
		return;
	}

	ptry = m_shader_resource_views[0];
	ptru = m_shader_resource_views[1];
	ptrv = m_shader_resource_views[2];
}

void TextureD3D11::upload(unsigned char* ych, unsigned char* uch, unsigned char* vch)
{
	if (m_device == nullptr || ych == nullptr || uch == nullptr || vch == nullptr)
	{
		LOG("TextureD3D11::upload - invalid texture state or source planes");
		return;
	}

	ID3D11DeviceContext* ctx = nullptr;
	m_device->GetImmediateContext(&ctx);
	if (ctx == nullptr)
	{
		LOG("TextureD3D11::upload - immediate context is unavailable");
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource[TEXTURE_COUNT]{};
	int mapped_count = 0;
	for (int i = 0; i < TEXTURE_COUNT; ++i) {
		const HRESULT result = ctx->Map(
			m_textures[i], 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource[i]);
		if (FAILED(result) || mappedResource[i].pData == nullptr)
		{
			LOG("TextureD3D11::upload - plane %d Map failed (0x%08x)", i, result);
			for (int mapped = 0; mapped < mapped_count; ++mapped)
				ctx->Unmap(m_textures[mapped], 0);
			ctx->Release();
			return;
		}
		++mapped_count;
	}

	//	Consider padding.
	UINT rowPitchY = mappedResource[0].RowPitch;
	UINT rowPitchUV = mappedResource[1].RowPitch;

	uint8_t* ptrMappedY = (uint8_t*)(mappedResource[0].pData);
	uint8_t* ptrMappedU = (uint8_t*)(mappedResource[1].pData);
	uint8_t* ptrMappedV = (uint8_t*)(mappedResource[2].pData);

	// These copies already execute on Unity's render thread. Creating and
	// joining worker threads per frame costs more than three linear copies.
	for (unsigned int row = 0; row < m_height_y; ++row)
		std::memcpy(ptrMappedY + row * rowPitchY,
			ych + row * m_width_y, m_width_y);
	for (unsigned int row = 0; row < m_height_uv; ++row)
	{
		std::memcpy(ptrMappedU + row * rowPitchUV,
			uch + row * m_width_uv, m_width_uv);
		std::memcpy(ptrMappedV + row * mappedResource[2].RowPitch,
			vch + row * m_width_uv, m_width_uv);
	}

	for (int i = 0; i < TEXTURE_COUNT; i++) {
		ctx->Unmap(m_textures[i], 0);
	}
	ctx->Release();
}

} // namespace openvolumetric::unity
