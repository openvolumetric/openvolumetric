#pragma once

#include <stdint.h>

#if defined(_WIN32)
#define OPENVOLUMETRIC_API __declspec(dllexport) __stdcall
#else
#define OPENVOLUMETRIC_API __attribute__((visibility("default")))
#endif
#define OPENVOLUMETRIC_ABI_VERSION_MAJOR 1u
#define OPENVOLUMETRIC_ABI_VERSION_MINOR 0u
#define OPENVOLUMETRIC_ABI_VERSION_PATCH 0u
#define OPENVOLUMETRIC_ABI_STRING_SMALL 128u
#define OPENVOLUMETRIC_ABI_STRING_LARGE 512u

/*
 * Stable C ABI consumed by OpenVolumetricDecoder.cs.
 *
 * Ownership and lifetime:
 * - openvolumetric_player_create allocates an opaque handle. Exactly one call
 *   to openvolumetric_player_destroy releases it; destroy must not race another
 *   call using the same handle.
 * - All input strings are borrowed UTF-8 and need remain valid only for the
 *   duration of the call.
 * - Snapshot structures and error buffers are caller-owned. Native code never
 *   retains their addresses. Set struct_size before every snapshot call.
 * - Graphics pointers and mesh-buffer handles are borrowed Unity resources;
 *   they must outlive playback and be unregistered by destroying the player
 *   before Unity releases them.
 * - GetRenderEventFunc is owned by Unity. The integer returned by
 *   openvolumetric_player_get_render_event_id is routing data only, not a
 *   player handle.
 *
 * Threading:
 * normal API and render calls are synchronized against destruction. Unity
 * object/resource setup remains on the main thread, rendering on the render
 * thread, and DSP reads on Unity's audio thread.
 */

#if defined(__cplusplus)
extern "C" {
#endif
	typedef struct OpenVolumetricPlayer OpenVolumetricPlayer;

	typedef enum OpenVolumetricResult
	{
		OPENVOLUMETRIC_RESULT_OK = 0,
		OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT = 1,
		OPENVOLUMETRIC_RESULT_INVALID_HANDLE = 2,
		OPENVOLUMETRIC_RESULT_UNSUPPORTED_FORMAT = 3,
		OPENVOLUMETRIC_RESULT_CORRUPT_DATA = 4,
		OPENVOLUMETRIC_RESULT_NETWORK_FAILURE = 5,
		OPENVOLUMETRIC_RESULT_TIMEOUT = 6,
		OPENVOLUMETRIC_RESULT_CANCELLED = 7,
		OPENVOLUMETRIC_RESULT_DECODER_FAILURE = 8,
		OPENVOLUMETRIC_RESULT_NOT_READY = 9,
		OPENVOLUMETRIC_RESULT_INTERNAL_FAILURE = 10
	} OpenVolumetricResult;

	typedef struct OpenVolumetricApiVersionV1
	{
		uint32_t struct_size;
		uint32_t major;
		uint32_t minor;
		uint32_t patch;
	} OpenVolumetricApiVersionV1;

	typedef struct OpenVolumetricMediaInfoV1
	{
		uint32_t struct_size;
		int32_t width;
		int32_t height;
		double frame_rate;
		double duration;
		int32_t has_audio;
		int32_t audio_sample_rate;
		int32_t audio_channels;
	} OpenVolumetricMediaInfoV1;

	typedef struct OpenVolumetricRuntimeSnapshotV1
	{
		uint32_t struct_size;
		int32_t input_state;
		int32_t remote;
		int64_t resource_size_bytes;
		uint64_t cached_bytes;
		uint64_t downloaded_bytes;
		uint64_t transfer_throughput_bits_per_second;
		uint64_t request_count;
		uint64_t recovery_count;
		int32_t fragmented;
		int64_t active_fragment;
		uint64_t fragment_count;
		uint64_t cached_fragment_count;
		double audio_read_time;
		double audio_buffered_duration;
		uint64_t audio_underrun_count;
		double last_presented_time;
		double adaptive_policy_throughput_bits_per_second;
	} OpenVolumetricRuntimeSnapshotV1;

	typedef struct OpenVolumetricAdaptiveSwitchSnapshotV1
	{
		uint32_t struct_size;
		int32_t state;
		uint64_t generation;
		uint64_t switch_count;
		double boundary_time;
		char active_representation[OPENVOLUMETRIC_ABI_STRING_SMALL];
		char pending_representation[OPENVOLUMETRIC_ABI_STRING_SMALL];
		char reason[OPENVOLUMETRIC_ABI_STRING_LARGE];
	} OpenVolumetricAdaptiveSwitchSnapshotV1;

	typedef struct OpenVolumetricCentroidV1
	{
		uint32_t struct_size;
		float x;
		float y;
		float z;
	} OpenVolumetricCentroidV1;

	typedef struct OpenVolumetricAdaptiveSelectionRequestV1
	{
		uint32_t struct_size;
		const char* manifest_json;
		const char* manifest_location;
		int32_t quality;
		uint32_t maximum_texture_width;
		uint32_t maximum_texture_height;
		uint64_t maximum_texture_bitrate;
		uint64_t maximum_geometry_bitrate;
		uint64_t maximum_bandwidth;
	} OpenVolumetricAdaptiveSelectionRequestV1;

	typedef struct OpenVolumetricAdaptiveRepresentationV1
	{
		uint32_t struct_size;
		uint64_t bandwidth;
		char id[OPENVOLUMETRIC_ABI_STRING_SMALL];
		char resource[OPENVOLUMETRIC_ABI_STRING_LARGE];
	} OpenVolumetricAdaptiveRepresentationV1;

	typedef struct OpenVolumetricAdaptiveSelectionV1
	{
		uint32_t struct_size;
		uint64_t measured_throughput_bits_per_second;
		uint32_t representation_count;
		double segment_duration;
		char representation_id[OPENVOLUMETRIC_ABI_STRING_SMALL];
		char resource_uri[OPENVOLUMETRIC_ABI_STRING_LARGE];
		char resolved_resource[OPENVOLUMETRIC_ABI_STRING_LARGE];
		char decision_reason[OPENVOLUMETRIC_ABI_STRING_LARGE];
		char error[OPENVOLUMETRIC_ABI_STRING_LARGE];
	} OpenVolumetricAdaptiveSelectionV1;

	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_get_api_version(
		OpenVolumetricApiVersionV1* version);
	OPENVOLUMETRIC_API void openvolumetric_open_external_console();
	OPENVOLUMETRIC_API void openvolumetric_close_external_console();
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_create(
		OpenVolumetricPlayer** player);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_destroy(
		OpenVolumetricPlayer* player);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_render_event_id(
		OpenVolumetricPlayer* player, int32_t* event_id);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_set_time(
		OpenVolumetricPlayer* player, double time);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_open(
		OpenVolumetricPlayer* player, const char* resource);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_start(
		OpenVolumetricPlayer* player);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_stop(
		OpenVolumetricPlayer* player);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_seek(
		OpenVolumetricPlayer* player, double time);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_error(
		OpenVolumetricPlayer* player, char* buffer, uint32_t capacity,
		uint32_t* required_capacity, OpenVolumetricResult* category);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_media_info(
		OpenVolumetricPlayer* player, OpenVolumetricMediaInfoV1* info);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_runtime_snapshot(
		OpenVolumetricPlayer* player, OpenVolumetricRuntimeSnapshotV1* snapshot);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_adaptive_switch_snapshot(
		OpenVolumetricPlayer* player,
		OpenVolumetricAdaptiveSwitchSnapshotV1* snapshot);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_centroid(
		OpenVolumetricPlayer* player, OpenVolumetricCentroidV1* centroid);

	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_configure_adaptive(
		OpenVolumetricPlayer* player, const char* representation_id);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_clear_adaptive_policy(
		OpenVolumetricPlayer* player);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_add_adaptive_representation(
		OpenVolumetricPlayer* player, const char* representation_id,
		const char* resource, uint64_t bandwidth);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_update_adaptive_policy(
		OpenVolumetricPlayer* player, double now, double presentation_time,
		double duration, double segment_duration, int32_t* action);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_request_adaptive_index(
		OpenVolumetricPlayer* player, uint64_t target_index, double now,
		double presentation_time, double duration, double segment_duration,
		int32_t* action);

	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_schedule_dsp_audio(
		OpenVolumetricPlayer* player, uint64_t dsp_start_tick,
		double media_start_time);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_stop_dsp_audio(
		OpenVolumetricPlayer* player);
	OPENVOLUMETRIC_API double openvolumetric_get_dsp_audio_time();

	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_texture_pointers(
		OpenVolumetricPlayer* player, void** y, void** u, void** v);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_register_texture_pointers(
		OpenVolumetricPlayer* player, void* y, void* u, void* v);
	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_set_mesh_buffers(
		OpenVolumetricPlayer* player, void* index_buffer, int32_t index_count,
		void* vertex_buffer, int32_t vertex_count);

	OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_adaptive_select(
		const OpenVolumetricAdaptiveSelectionRequestV1* request,
		OpenVolumetricAdaptiveSelectionV1* selection,
		OpenVolumetricAdaptiveRepresentationV1* representations,
		uint32_t representation_capacity);
#if defined(__cplusplus)
}
#endif
