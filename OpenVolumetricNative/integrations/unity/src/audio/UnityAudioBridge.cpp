#include "UnityAudioBridge.h"

#include <UnityOpenVolumetricPlayer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

namespace openvolumetric::unity
{
namespace
{
std::atomic<UnityOpenVolumetricPlayer*> g_player{nullptr};
std::atomic<std::uint32_t> g_readers{0};
std::atomic<std::uint64_t> g_start_tick{0};
std::atomic<double> g_media_start_time{0.0};
std::atomic<double> g_audio_time{-1.0};
std::atomic<bool> g_active{false};
std::atomic<std::uint32_t> g_media_sample_rate{0};

// The bridge currently permits one DSP consumer, so its sample-rate converter
// can retain state here without synchronization. Scheduling disables the
// callback and waits for an in-flight block before resetting this state.
constexpr std::size_t k_resample_capacity_frames = 8192;
std::array<float, k_resample_capacity_frames * 2> g_resample_input{};
std::size_t g_resample_frames = 0;
double g_resample_position = 0.0;
}

void schedule_dsp_audio(
	UnityOpenVolumetricPlayer* player,
	std::uint64_t dsp_start_tick,
	double media_start_time)
{
	g_active.store(false, std::memory_order_release);
	while (g_readers.load(std::memory_order_acquire) != 0)
		std::this_thread::yield();
	g_resample_frames = 0;
	g_resample_position = 0.0;
	g_start_tick.store(dsp_start_tick, std::memory_order_release);
	g_media_start_time.store(media_start_time, std::memory_order_release);
	g_audio_time.store(media_start_time, std::memory_order_release);
	g_media_sample_rate.store(
		player != nullptr
			? static_cast<std::uint32_t>(
				std::max(player->media_info().audio_sample_rate, 0))
			: 0,
		std::memory_order_release);
	g_player.store(player, std::memory_order_release);
	g_active.store(player != nullptr, std::memory_order_release);
}

void stop_dsp_audio(UnityOpenVolumetricPlayer* player)
{
	UnityOpenVolumetricPlayer* current =
		g_player.load(std::memory_order_acquire);
	if (player != nullptr && current != player)
		return;

	g_active.store(false, std::memory_order_release);
	g_player.store(nullptr, std::memory_order_release);
	while (g_readers.load(std::memory_order_acquire) != 0)
		std::this_thread::yield();
	g_resample_frames = 0;
	g_resample_position = 0.0;
	g_audio_time.store(-1.0, std::memory_order_release);
}

void process_dsp_audio(
	float* output,
	unsigned int frame_count,
	int channel_count,
	std::uint64_t block_start_tick,
	std::uint32_t sample_rate)
{
	const std::size_t sample_count =
		static_cast<std::size_t>(frame_count) *
		static_cast<std::size_t>(std::max(channel_count, 0));
	if (output == nullptr)
		return;
	std::fill(output, output + sample_count, 0.0f);
	if (!g_active.load(std::memory_order_acquire) ||
		channel_count != 2 ||
		sample_rate == 0)
		return;

	const std::uint64_t start_tick =
		g_start_tick.load(std::memory_order_acquire);
	const std::uint64_t block_end_tick = block_start_tick + frame_count;
	if (block_end_tick <= start_tick)
		return;

	const unsigned int silent_frames = block_start_tick < start_tick
		? static_cast<unsigned int>(start_tick - block_start_tick)
		: 0;
	const unsigned int frames_to_read = frame_count - silent_frames;

	g_readers.fetch_add(1, std::memory_order_acq_rel);
	UnityOpenVolumetricPlayer* player =
		g_player.load(std::memory_order_acquire);
	if (g_active.load(std::memory_order_acquire) && player != nullptr)
	{
		float* audible_output =
			output + static_cast<std::size_t>(silent_frames) * 2u;
		const std::uint32_t media_sample_rate =
			g_media_sample_rate.load(std::memory_order_acquire);
		if (media_sample_rate == sample_rate)
		{
			player->read_audio(
				audible_output,
				static_cast<int>(frames_to_read * 2u));
		}
		else if (media_sample_rate > 0)
		{
			const double source_step =
				static_cast<double>(media_sample_rate) /
				static_cast<double>(sample_rate);
			const std::size_t required_frames = std::min(
				k_resample_capacity_frames,
				static_cast<std::size_t>(std::floor(
					g_resample_position +
					static_cast<double>(frames_to_read) * source_step)) +
					2u);
			if (required_frames > g_resample_frames)
			{
				const std::size_t requested =
					required_frames - g_resample_frames;
				player->read_audio(
					g_resample_input.data() + g_resample_frames * 2u,
					static_cast<int>(requested * 2u));
				g_resample_frames = required_frames;
			}

			unsigned int produced = 0;
			for (; produced < frames_to_read; ++produced)
			{
				const std::size_t first =
					static_cast<std::size_t>(g_resample_position);
				if (first + 1u >= g_resample_frames)
					break;
				const float fraction = static_cast<float>(
					g_resample_position - static_cast<double>(first));
				for (std::size_t channel = 0; channel < 2u; ++channel)
				{
					const float a =
						g_resample_input[first * 2u + channel];
					const float b =
						g_resample_input[(first + 1u) * 2u + channel];
					audible_output[
						static_cast<std::size_t>(produced) * 2u +
						channel] = a + (b - a) * fraction;
				}
				g_resample_position += source_step;
			}

			const std::size_t consumed = std::min(
				static_cast<std::size_t>(g_resample_position),
				g_resample_frames);
			const std::size_t retained = g_resample_frames - consumed;
			std::memmove(
				g_resample_input.data(),
				g_resample_input.data() + consumed * 2u,
				retained * 2u * sizeof(float));
			g_resample_frames = retained;
			g_resample_position -= static_cast<double>(consumed);
		}
		const std::uint64_t audible_tick =
			std::max(block_start_tick, start_tick);
		g_audio_time.store(
			g_media_start_time.load(std::memory_order_acquire) +
				static_cast<double>(audible_tick - start_tick) /
					static_cast<double>(sample_rate),
			std::memory_order_release);
	}
	g_readers.fetch_sub(1, std::memory_order_acq_rel);
}

double dsp_audio_time()
{
	return g_audio_time.load(std::memory_order_acquire);
}

} // namespace openvolumetric::unity
