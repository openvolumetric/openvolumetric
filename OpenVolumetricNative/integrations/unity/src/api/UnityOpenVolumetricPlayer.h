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
	/// Takes ownership of one texture uploader and mesh-buffer uploader.
	UnityOpenVolumetricPlayer(
		int id,
		std::unique_ptr<ITexture> texture,
		std::unique_ptr<IMeshBuffer> mesh_buffer);
	/// Stops decoding before releasing Unity-facing graphics adapters.
	~UnityOpenVolumetricPlayer();

	UnityOpenVolumetricPlayer(const UnityOpenVolumetricPlayer&) = delete;
	UnityOpenVolumetricPlayer& operator=(
		const UnityOpenVolumetricPlayer&) = delete;

	/// Stable integer used by the exported C ABI instance registry.
	int id() const { return m_id; }
	/// Opens one local path or HTTP(S) representation.
	bool open(const char* path);
	/// Starts native media and geometry workers.
	bool start();
	/// Stops workers while retaining opened media and graphics resources.
	bool stop();
	/// Seeks every media modality to seconds on the shared timeline.
	bool seek(double time);
	/// Begins generation-safe preparation for an aligned representation switch.
	bool request_adaptive_switch(
		const char* resource,
		const char* representation_id,
		double boundary_time,
		const char* reason);
	/// Associates the initially opened resource with its manifest identifier.
	void set_active_representation_id(const char* representation_id);
	/// Returns a thread-safe adaptive transition snapshot.
	AdaptiveSwitchInfo adaptive_switch_info() const;
	/// Cancels pending representation preparation without stopping active media.
	void cancel_adaptive_switch();
	/// Idempotently stops workers and releases opened native media state.
	void close();

	/// Publishes Unity's desired media time for the next render callback.
	void set_presentation_time(double time);
	/// Uploads one synchronized presentation; call only on Unity's render thread.
	int render();

	/// Returns immutable metadata for the currently opened presentation.
	const OpenVolumetricMediaInfo& media_info() const;
	/// Returns current input/cache diagnostics.
	OpenVolumetricBufferInfo buffer_info() const;
	/// Returns current decoded-audio diagnostics.
	OpenVolumetricAudioBufferInfo audio_buffer_info() const;
	/// Returns a thread-safe copy of the latest playback or adaptation error.
	std::string error() const;
	/// Fills interleaved float PCM from Unity's native DSP callback.
	int read_audio(float* output, int sample_count);
	/// Returns the last presentation uploaded by the render thread.
	double last_presented_time() const;
	/// Copies a consistent snapshot of the last uploaded mesh centroid.
	bool geometry_centroid(float& x, float& y, float& z) const;

	/// Borrowed texture adapter owned by this player; never delete it.
	ITexture* texture() const { return m_texture.get(); }
	/// Borrowed mesh adapter owned by this player; never delete it.
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
