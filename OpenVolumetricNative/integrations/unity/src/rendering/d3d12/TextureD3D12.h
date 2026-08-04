#pragma once

#include "D3D12UnityContext.h"
#include <ITexture.h>
#include <wrl/client.h>
#include <array>

namespace openvolumetric::unity
{
class TextureD3D12 final : public ITexture
{
public:
	~TextureD3D12() override;
	int init(void*, unsigned int, unsigned int) override;
	void get_resource_pointers(void*&, void*&, void*&) override;
	void upload(unsigned char*, unsigned char*, unsigned char*) override;
	void destroy() override;

private:
	static constexpr unsigned int kUploadSlotCount = 4;
	struct UploadSlot
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		UINT64 fence = 0;
	};
	struct Plane
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		std::array<UploadSlot, kUploadSlotCount> uploads;
		unsigned int width = 0;
		unsigned int height = 0;
		unsigned int source_stride = 0;
		unsigned int row_pitch = 0;
		unsigned int next_slot = 0;
	};
	bool create_plane(Plane&, unsigned int, unsigned int, unsigned int);
	D3D12UnityContext* m_context = nullptr;
	std::array<Plane, TEXTURE_COUNT> m_planes;
};
}
