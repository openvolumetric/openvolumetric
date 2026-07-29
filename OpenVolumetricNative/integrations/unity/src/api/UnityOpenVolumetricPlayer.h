#pragma once

#include <IMeshBuffer.h>
#include <ITexture.h>
#include <OpenVolumetricPlayer.h>

#include <atomic>
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
	void close();

	void set_presentation_time(double time);
	int render();

	const OpenVolumetricMediaInfo& media_info() const;
	const std::string& error() const;
	int read_audio(float* output, int sample_count);
	double last_presented_time() const;

	ITexture* texture() const { return m_texture.get(); }
	IMeshBuffer* mesh_buffer() const { return m_mesh_buffer.get(); }

private:
	int m_id;
	OpenVolumetricPlayer m_player;
	std::unique_ptr<ITexture> m_texture;
	std::unique_ptr<IMeshBuffer> m_mesh_buffer;
	std::atomic<double> m_presentation_time{0.0};
	std::atomic<double> m_last_presented_time{-1.0};
};

} // namespace openvolumetric::unity
