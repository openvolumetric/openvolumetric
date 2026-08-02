#include "AdaptivePolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace openvolumetric
{
namespace
{

constexpr double kDowngradeSafetyFactor = 1.2;
constexpr double kUpgradeSafetyFactor = 1.75;
constexpr double kDowngradeDwellSeconds = 2.0;
constexpr double kUpgradeDwellSeconds = 10.0;
constexpr double kPreparationSegments = 4.0;

} // namespace

void AdaptivePolicy::configure(
	std::vector<AdaptivePolicyRepresentation> representations)
{
	std::stable_sort(
		representations.begin(), representations.end(),
		[](const auto& left, const auto& right)
		{
			return left.bandwidth < right.bandwidth;
		});
	m_representations = std::move(representations);
	reset();
}

void AdaptivePolicy::reset()
{
	m_smoothed_throughput_bps = 0.0;
	m_last_sample_time = -1.0;
	m_last_downloaded_bytes = 0;
	m_downgrade_started = -1.0;
	m_upgrade_started = -1.0;
	m_retry_after = -1.0;
	m_consecutive_failures = 0;
	m_observed_switch_count = 0;
	m_handled_failure_generation = 0;
}

std::size_t AdaptivePolicy::active_index(const std::string& id) const
{
	const auto found = std::find_if(
		m_representations.begin(), m_representations.end(),
		[&id](const auto& representation)
		{
			return representation.id == id;
		});
	return found == m_representations.end()
		? m_representations.size()
		: static_cast<std::size_t>(found - m_representations.begin());
}

void AdaptivePolicy::observe_throughput(
	const AdaptivePolicyObservation& observation)
{
	if (observation.transfer_throughput_bps > 0)
	{
		m_smoothed_throughput_bps =
			static_cast<double>(observation.transfer_throughput_bps);
	}
	if (m_last_sample_time < 0.0)
	{
		m_last_sample_time = observation.now;
		m_last_downloaded_bytes = observation.downloaded_bytes;
		return;
	}
	const double elapsed = observation.now - m_last_sample_time;
	if (elapsed < 1.0)
		return;
	if (observation.downloaded_bytes >= m_last_downloaded_bytes &&
		observation.transfer_throughput_bps == 0)
	{
		const std::uint64_t downloaded =
			observation.downloaded_bytes - m_last_downloaded_bytes;
		if (downloaded > 0)
		{
			const double measured =
				static_cast<double>(downloaded) * 8.0 / elapsed;
			m_smoothed_throughput_bps = m_smoothed_throughput_bps <= 0.0
				? measured
				: 0.75 * m_smoothed_throughput_bps + 0.25 * measured;
		}
	}
	m_last_downloaded_bytes = observation.downloaded_bytes;
	m_last_sample_time = observation.now;
}

AdaptivePolicyDecision AdaptivePolicy::switch_to(
	std::size_t target_index,
	const AdaptivePolicyObservation& observation,
	std::string reason,
	bool ignore_backoff)
{
	AdaptivePolicyDecision decision;
	if (target_index >= m_representations.size() ||
		observation.segment_duration <= 0.0 ||
		observation.duration <= 0.0)
		return decision;
	if (!ignore_backoff && observation.now < m_retry_after)
	{
		decision.action = AdaptivePolicyAction::RetryLater;
		decision.retry_time = m_retry_after;
		decision.reason = "Adaptive preparation is in failure backoff.";
		return decision;
	}
	const double lead = observation.segment_duration * kPreparationSegments;
	const double boundary = std::ceil(
		(observation.presentation_time + lead) /
		observation.segment_duration) * observation.segment_duration;
	if (!std::isfinite(boundary) || boundary >= observation.duration)
		return decision;
	const auto& target = m_representations[target_index];
	decision.action = AdaptivePolicyAction::Switch;
	decision.target_index = target_index;
	decision.boundary_time = boundary;
	decision.target_representation = target.id;
	decision.target_resource = target.resource;
	decision.reason = std::move(reason);
	m_downgrade_started = -1.0;
	m_upgrade_started = -1.0;
	return decision;
}

AdaptivePolicyDecision AdaptivePolicy::update(
	const AdaptivePolicyObservation& observation)
{
	AdaptivePolicyDecision decision;
	if (m_representations.size() < 2)
		return decision;

	if (observation.switch_count != m_observed_switch_count)
	{
		m_observed_switch_count = observation.switch_count;
		m_last_sample_time = observation.now;
		m_last_downloaded_bytes = observation.downloaded_bytes;
		m_downgrade_started = -1.0;
		m_upgrade_started = -1.0;
		m_retry_after = -1.0;
		m_consecutive_failures = 0;
	}
	if (observation.switch_state == AdaptivePolicySwitchState::Failed)
	{
		decision.action = AdaptivePolicyAction::RetryLater;
		if (observation.switch_generation != m_handled_failure_generation)
		{
			m_handled_failure_generation = observation.switch_generation;
			++m_consecutive_failures;
			const int exponent = std::min(m_consecutive_failures - 1, 3);
			m_retry_after = observation.now + std::min(
				30.0, 5.0 * std::pow(2.0, exponent));
			decision.cancel_failed_switch = true;
		}
		decision.retry_time = m_retry_after;
		decision.reason = "Adaptive preparation failed; retrying after backoff.";
		m_downgrade_started = -1.0;
		m_upgrade_started = -1.0;
		return decision;
	}
	if (observation.switch_state != AdaptivePolicySwitchState::Stable)
		return decision;

	observe_throughput(observation);
	if (observation.now < m_retry_after)
	{
		decision.action = AdaptivePolicyAction::RetryLater;
		decision.retry_time = m_retry_after;
		decision.reason = "Adaptive preparation is in failure backoff.";
		return decision;
	}

	const std::size_t current = active_index(observation.active_representation);
	if (current >= m_representations.size())
		return decision;
	if (current > 0)
	{
		const bool rebuffering = observation.input_state ==
			AdaptivePolicyInputState::Rebuffering;
		const bool insufficient = rebuffering ||
			(m_smoothed_throughput_bps > 0.0 &&
			 m_smoothed_throughput_bps <
				static_cast<double>(m_representations[current].bandwidth) *
				kDowngradeSafetyFactor);
		if (insufficient)
		{
			if (m_downgrade_started < 0.0)
				m_downgrade_started = observation.now;
			if (rebuffering || observation.now - m_downgrade_started >=
				kDowngradeDwellSeconds)
			{
				return switch_to(
					current - 1, observation,
					rebuffering
						? "Active input rebuffered; preparing a lower representation."
						: "Sustained throughput lacks headroom for the active representation.",
					false);
			}
			return decision;
		}
	}
	m_downgrade_started = -1.0;

	if (current + 1 < m_representations.size())
	{
		const auto& next = m_representations[current + 1];
		const bool headroom = observation.input_state ==
			AdaptivePolicyInputState::Ready &&
			m_smoothed_throughput_bps >=
				static_cast<double>(next.bandwidth) * kUpgradeSafetyFactor;
		if (headroom)
		{
			if (m_upgrade_started < 0.0)
				m_upgrade_started = observation.now;
			if (observation.now - m_upgrade_started >= kUpgradeDwellSeconds)
			{
				return switch_to(
					current + 1, observation,
					"Sustained throughput and input stability permit a higher representation.",
					false);
			}
		}
		else
		{
			m_upgrade_started = -1.0;
		}
	}
	return decision;
}

AdaptivePolicyDecision AdaptivePolicy::request(
	std::size_t target_index,
	const AdaptivePolicyObservation& observation)
{
	if (target_index < m_representations.size() &&
		m_representations[target_index].id == observation.active_representation)
		return {};
	return switch_to(
		target_index, observation,
		"Developer requested a manual adaptive transition.", true);
}

double AdaptivePolicy::smoothed_throughput_bps() const
{
	return m_smoothed_throughput_bps;
}

double AdaptivePolicy::retry_after() const
{
	return m_retry_after;
}

} // namespace openvolumetric
