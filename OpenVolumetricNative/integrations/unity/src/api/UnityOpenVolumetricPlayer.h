#pragma once

#include <IMeshBuffer.h>
#include <ITexture.h>
#include <AdaptivePlayerCoordinator.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace openvolumetric::unity
{

/// Adapts the engine-neutral player to Unity-owned graphics resources.
///
/// Decode, synchronization, seeking, and frame ownership remain in
/// OpenVolumetricPlayer. This class runs only the final texture/mesh upload on
/// Unity's render thread and exposes the resources needed by the C ABI.
class UnityOpenVolumetricPlayer final
{
public:
	UnityOpenVolumetricPlayer(
		int id,
		std::unique_ptr<ITexture> texture,
		std::unique_ptr<IMeshBuffer> mesh_buffer);
	~UnityOpenVolumetricPlayer();

	UnityOpenVolumetricPlayer(const UnityOpenVolumetricPlayer&) = delete;
	UnityOpenVolumetricPlayer& operator=(
		const UnityOpenVolumetricPlayer&) = delete;

	int id() const { return m_id; }
	bool open(const char* path);
	bool start();
	bool stop();
	bool seek(double time);
	bool request_adaptive_switch(
		const char* resource,
		const char* representation_id,
		double boundary_time,
		const char* reason);
	void set_active_representation_id(const char* representation_id);
	AdaptiveSwitchInfo adaptive_switch_info() const;
	void cancel_adaptive_switch();
	void close();

	void set_presentation_time(double time);
	int render();

	const OpenVolumetricMediaInfo& media_info() const;
	OpenVolumetricBufferInfo buffer_info() const;
	OpenVolumetricAudioBufferInfo audio_buffer_info() const;
	const std::string& error() const;
	int read_audio(float* output, int sample_count);
	double last_presented_time() const;
	bool geometry_centroid(float& x, float& y, float& z) const;

	ITexture* texture() const { return m_texture.get(); }
	IMeshBuffer* mesh_buffer() const { return m_mesh_buffer.get(); }

private:
	int m_id;
	AdaptivePlayerCoordinator m_player;
	std::unique_ptr<ITexture> m_texture;
	std::unique_ptr<IMeshBuffer> m_mesh_buffer;
	std::atomic<double> m_presentation_time{0.0};
	std::atomic<double> m_last_presented_time{-1.0};
	std::atomic<float> m_centroid_x{0.0F};
	std::atomic<float> m_centroid_y{0.0F};
	std::atomic<float> m_centroid_z{0.0F};
	/// Even values publish a complete centroid; odd values denote an update.
	std::atomic<std::uint64_t> m_centroid_sequence{0};
};

} // namespace openvolumetric::unity
