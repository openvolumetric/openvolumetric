#pragma once

#include "OpenVolumetricPlayer.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace openvolumetric
{

enum class AdaptiveSwitchState
{
	Stable,
	Preparing,
	Ready,
	Failed
};

/** Observable state for one generation-safe representation transition. */
struct AdaptiveSwitchInfo
{
	AdaptiveSwitchState state = AdaptiveSwitchState::Stable;
	std::uint64_t generation = 0;
	std::uint64_t switch_count = 0;
	double boundary_time = 0.0;
	std::string active_representation;
	std::string pending_representation;
	std::string reason;
};

/**
 * Owns active and warming decoder sessions for boundary-safe adaptation.
 *
 * Preparation occurs on a private worker. A generation token prevents stale
 * work from publishing after seek, close, or a newer switch request. The
 * active shared pointer is exchanged only after the pending session has
 * decoded a complete texture/geometry presentation at the requested boundary.
 */
class AdaptivePlayerCoordinator final
{
public:
	AdaptivePlayerCoordinator();
	~AdaptivePlayerCoordinator();

	AdaptivePlayerCoordinator(const AdaptivePlayerCoordinator&) = delete;
	AdaptivePlayerCoordinator& operator=(
		const AdaptivePlayerCoordinator&) = delete;

	bool open(const char* path, std::string representation_id = {});
	bool start();
	void stop();
	void close();
	bool seek(double time);

	bool request_switch(
		std::string resource,
		std::string representation_id,
		double boundary_time,
		std::string reason);
	void set_active_representation_id(std::string representation_id);
	void cancel_pending_switch();

	const OpenVolumetricMediaInfo& media_info() const;
	OpenVolumetricBufferInfo buffer_info() const;
	OpenVolumetricAudioBufferInfo audio_buffer_info() const;
	const std::string& error() const;
	AdaptiveSwitchInfo switch_info() const;

	FrameMatchResult presentation(
		double requested_time,
		OpenVolumetricPresentation& output);
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
