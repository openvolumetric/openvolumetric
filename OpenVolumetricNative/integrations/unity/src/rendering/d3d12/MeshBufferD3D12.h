#pragma once

#include "D3D12UnityContext.h"
#include <IMeshBuffer.h>
#include <wrl/client.h>
#include <array>

namespace openvolumetric::unity
{
class MeshBufferD3D12 final : public IMeshBuffer
{
public:
	~MeshBufferD3D12() override;
	bool init(void*, void*, int, void*, int) override;
	bool update(Mesh*) override;
	void destroy() override;

private:
	static constexpr unsigned int kUploadSlotCount = 4;
	struct UploadSlot
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> index;
		Microsoft::WRL::ComPtr<ID3D12Resource> vertex;
		UINT64 fence = 0;
	};
	bool create_upload(Microsoft::WRL::ComPtr<ID3D12Resource>&, UINT64);
	D3D12UnityContext* m_context = nullptr;
	ID3D12Resource* m_index = nullptr;
	ID3D12Resource* m_vertex = nullptr;
	UINT64 m_index_bytes = 0;
	UINT64 m_vertex_bytes = 0;
	int m_index_count = 0;
	int m_vertex_count = 0;
	UINT64 m_index_stride = 0;
	UINT64 m_vertex_stride = 0;
	std::array<UploadSlot, kUploadSlotCount> m_uploads;
	unsigned int m_next_slot = 0;
};
}
