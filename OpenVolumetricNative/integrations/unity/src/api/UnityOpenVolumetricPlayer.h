#pragma once

#include <IMeshBuffer.h>
#include <ITexture.h>
#include <AdaptivePlayerCoordinator.h>
#include <AdaptivePolicy.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
	/// Associates the initially opened resource with its manifest identifier.
	void set_active_representation_id(const char* representation_id);
	/// Returns a thread-safe adaptive transition snapshot.
	AdaptiveSwitchInfo adaptive_switch_info() const;
	/// Replaces the policy ladder; call once for each manifest load.
	void clear_adaptive_policy();
	void add_adaptive_policy_representation(
		const char* id, const char* resource, std::uint64_t bandwidth);
	/// Evaluates shared automatic policy and starts any returned transition.
	AdaptivePolicyDecision update_adaptive_policy(
		const AdaptivePolicyObservation& observation);
	/// Requests a deterministic ladder entry while bypassing failure backoff.
	AdaptivePolicyDecision request_adaptive_policy(
		std::size_t target_index,
		const AdaptivePolicyObservation& observation);
	double adaptive_policy_throughput() const;
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
	AdaptivePolicy m_adaptive_policy;
	std::vector<AdaptivePolicyRepresentation> m_policy_representations;
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
