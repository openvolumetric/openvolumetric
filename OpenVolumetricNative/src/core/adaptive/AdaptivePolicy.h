#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openvolumetric
{

/// Transport readiness consumed without exposing a concrete byte source.
enum class AdaptivePolicyInputState
{
	Opening,
	Ready,
	Rebuffering,
	Error,
	Cancelled
};

/// Coordinator lifecycle snapshot consumed by the selection policy.
enum class AdaptivePolicySwitchState
{
	Stable,
	Preparing,
	Ready,
	Failed
};

/// One capability-compatible representation in ascending or arbitrary order.
struct AdaptivePolicyRepresentation
{
	std::string id;
	std::string resource;
	std::uint64_t bandwidth = 0;
};

/// Complete deterministic observation supplied once per host update.
struct AdaptivePolicyObservation
{
	double now = 0.0;
	double presentation_time = 0.0;
	double duration = 0.0;
	double segment_duration = 0.0;
	AdaptivePolicyInputState input_state = AdaptivePolicyInputState::Opening;
	std::uint64_t transfer_throughput_bps = 0;
	std::uint64_t downloaded_bytes = 0;
	AdaptivePolicySwitchState switch_state = AdaptivePolicySwitchState::Stable;
	std::uint64_t switch_generation = 0;
	std::uint64_t switch_count = 0;
	std::string active_representation;
};

/// Explicit result; resource opening remains the coordinator's responsibility.
enum class AdaptivePolicyAction
{
	Stay,
	Switch,
	RetryLater
};

struct AdaptivePolicyDecision
{
	AdaptivePolicyAction action = AdaptivePolicyAction::Stay;
	std::size_t target_index = 0;
	double boundary_time = 0.0;
	double retry_time = 0.0;
	bool cancel_failed_switch = false;
	std::string target_representation;
	std::string target_resource;
	std::string reason;
};

/// Engine-neutral adaptive bitrate policy with deterministic monotonic timing.
class AdaptivePolicy final
{
public:
	void configure(std::vector<AdaptivePolicyRepresentation> representations);
	void reset();
	AdaptivePolicyDecision update(const AdaptivePolicyObservation& observation);
	AdaptivePolicyDecision request(
		std::size_t target_index,
		const AdaptivePolicyObservation& observation);

	double smoothed_throughput_bps() const;
	double retry_after() const;

private:
	AdaptivePolicyDecision switch_to(
		std::size_t target_index,
		const AdaptivePolicyObservation& observation,
		std::string reason,
		bool ignore_backoff);
	void observe_throughput(const AdaptivePolicyObservation& observation);
	std::size_t active_index(const std::string& id) const;

	std::vector<AdaptivePolicyRepresentation> m_representations;
	double m_smoothed_throughput_bps = 0.0;
	double m_last_sample_time = -1.0;
	std::uint64_t m_last_downloaded_bytes = 0;
	double m_downgrade_started = -1.0;
	double m_upgrade_started = -1.0;
	double m_retry_after = -1.0;
	int m_consecutive_failures = 0;
	std::uint64_t m_observed_switch_count = 0;
	std::uint64_t m_handled_failure_generation = 0;
};

} // namespace openvolumetric
