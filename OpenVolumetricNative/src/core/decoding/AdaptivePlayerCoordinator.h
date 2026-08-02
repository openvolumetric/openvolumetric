#pragma once

#include "OpenVolumetricPlayer.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace openvolumetric
{

/// Lifecycle of a requested representation transition.
enum class AdaptiveSwitchState
{
	/// No candidate is active and the current session owns presentation.
	Stable,
	/// A background worker is opening, seeking, and priming the candidate.
	Preparing,
	/// The candidate is primed and awaiting its atomic boundary commit.
	Ready,
	/// Candidate preparation or boundary commit failed without replacing active.
	Failed
};

/// Observable state for one generation-safe representation transition.
struct AdaptiveSwitchInfo
{
	/// Current transition lifecycle.
	AdaptiveSwitchState state = AdaptiveSwitchState::Stable;
	/// Cancellation generation; stale workers may not publish another generation.
	std::uint64_t generation = 0;
	/// Number of candidates committed since the coordinator opened.
	std::uint64_t switch_count = 0;
	/// Media timestamp at which every modality changes representation.
	double boundary_time = 0.0;
	/// Identifier of the session currently supplying presentations.
	std::string active_representation;
	/// Identifier being prepared, or empty when no transition is active.
	std::string pending_representation;
	/// Human-readable policy or developer reason for the current request.
	std::string reason;
};

/// Owns active and warming decoder sessions for boundary-safe adaptation.
///
/// Preparation occurs on a private worker. A generation token prevents stale
/// work from publishing after seek, close, or a newer switch request. The
/// active shared pointer is exchanged only after the pending session has
/// decoded a complete texture/geometry presentation at the requested boundary.
/// Public methods may be called by engine game/audio/render threads as noted;
/// mutable session publication and diagnostics are serialized by m_mutex.
class AdaptivePlayerCoordinator final
{
public:
	/// Constructs an empty coordinator with no active session.
	AdaptivePlayerCoordinator();
	/// Cancels preparation, joins its worker, and closes every owned session.
	~AdaptivePlayerCoordinator();

	AdaptivePlayerCoordinator(const AdaptivePlayerCoordinator&) = delete;
	AdaptivePlayerCoordinator& operator=(
		const AdaptivePlayerCoordinator&) = delete;

	/// Replaces existing state with one opened representation.
	bool open(const char* path, std::string representation_id = {});
	/// Starts the active session's decoder workers.
	bool start();
	/// Stops active workers and cancels candidate preparation; idempotent.
	void stop();
	/// Releases all sessions and resets observable state; idempotent.
	void close();
	/// Cancels candidate work and seeks every active modality synchronously.
	bool seek(double time);

	/// Opens and primes a candidate on the private preparation worker.
	bool request_switch(
		std::string resource,
		std::string representation_id,
		double boundary_time,
		std::string reason);
	/// Labels the initially opened representation without starting a switch.
	void set_active_representation_id(std::string representation_id);
	/// Cancels and joins candidate preparation while retaining active playback.
	void cancel_pending_switch();

	/// Returns immutable metadata established when the active session opened.
	const OpenVolumetricMediaInfo& media_info() const;
	/// Returns a thread-safe snapshot of active input/cache diagnostics.
	OpenVolumetricBufferInfo buffer_info() const;
	/// Returns a thread-safe snapshot of the active decoded-audio queue.
	OpenVolumetricAudioBufferInfo audio_buffer_info() const;
	/// Returns a locked copy of the most recent coordinator/session error.
	std::string error() const;
	/// Returns a thread-safe transition-state snapshot.
	AdaptiveSwitchInfo switch_info() const;

	/// Selects one complete texture/geometry presentation for the engine.
	FrameMatchResult presentation(
		double requested_time,
		OpenVolumetricPresentation& output);
	/// Fills interleaved PCM and owns sample-exact audio-boundary commits.
	int read_audio(float* output, int sample_count);

private:
	struct Session;

	std::shared_ptr<Session> active_session() const;
	void join_preparation_worker();
	bool commit_if_ready(
		double requested_time,
		OpenVolumetricPresentation& output,
		bool& used_primed_presentation);
	bool commit_pending_locked();

	mutable std::mutex m_mutex;
	std::shared_ptr<Session> m_active;
	std::shared_ptr<Session> m_preparing;
	std::shared_ptr<Session> m_pending;
	std::shared_ptr<Session> m_retired;
	std::thread m_preparation_worker;
	AdaptiveSwitchInfo m_switch_info;
	OpenVolumetricMediaInfo m_media_info;
	std::string m_error;
	bool m_started = false;
};

} // namespace openvolumetric
