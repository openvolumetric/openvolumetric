#pragma once

#include <cstdint>

namespace openvolumetric::unity
{

class UnityOpenVolumetricPlayer;

/// Activates the player consumed by Unity's native DSP callback.
void schedule_dsp_audio(
	UnityOpenVolumetricPlayer* player,
	std::uint64_t dsp_start_tick,
	double media_start_time);

/// Stops DSP consumption and waits for an in-flight mixer block to finish.
void stop_dsp_audio(UnityOpenVolumetricPlayer* player);

/// Real-time callback entry: fills one Unity mixer block without locking.
void process_dsp_audio(
	float* output,
	unsigned int frame_count,
	int channel_count,
	std::uint64_t block_start_tick,
	std::uint32_t sample_rate);

/// Returns the media timestamp published for the latest mixer block.
double dsp_audio_time();

} // namespace openvolumetric::unity
