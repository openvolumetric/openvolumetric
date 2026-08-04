#include "openvolumetric-unity-api.h"

#if defined(_WIN32)
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#endif

#include <Unity/IUnityInterface.h>
#include <Unity/IUnityGraphics.h>
#if defined(_WIN32)
#include <Unity/IUnityGraphicsD3D11.h>
#include <Unity/IUnityGraphicsD3D12.h>
#elif defined(__APPLE__)
#include <Unity/IUnityGraphicsMetal.h>
#import <Metal/Metal.h>
#elif defined(__ANDROID__)
#include <Unity/IUnityGraphicsVulkan.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <cstring>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#include <MeshBufferD3D11.h>
#include <TextureD3D11.h>
#include <D3D12UnityContext.h>
#include <MeshBufferD3D12.h>
#include <TextureD3D12.h>
#elif defined(__APPLE__)
#include <MeshBufferMetal.h>
#include <TextureMetal.h>
#elif defined(__ANDROID__)
#include <MeshBufferVulkan.h>
#include <TextureVulkan.h>
#endif
#include "UnityOpenVolumetricPlayer.h"
#include <UnityAudioBridge.h>
#include <AdaptiveSelection.h>
#include <Logger.h>

using openvolumetric::Logger;
using openvolumetric::unity::UnityOpenVolumetricPlayer;
#if defined(_WIN32)
using openvolumetric::unity::MeshBufferD3D11;
using openvolumetric::unity::TextureD3D11;
using openvolumetric::unity::D3D12UnityContext;
using openvolumetric::unity::MeshBufferD3D12;
using openvolumetric::unity::TextureD3D12;
#elif defined(__APPLE__)
using openvolumetric::unity::MeshBufferMetal;
using openvolumetric::unity::TextureMetal;
#elif defined(__ANDROID__)
using openvolumetric::unity::MeshBufferVulkan;
using openvolumetric::unity::TextureVulkan;
#endif

struct OpenVolumetricPlayer
{
	std::uint64_t magic = 0;
	int event_id = -1;
};

namespace
{
constexpr std::uint64_t kPlayerHandleMagic = 0x4f564f4c504c4159ULL;
/// Owns all live Unity players. Normal API/render calls retain a shared lock
/// for the complete operation; destruction takes the exclusive lock, so a
/// queued render event cannot use an instance while it is being deleted.
std::unordered_map<int, std::unique_ptr<UnityOpenVolumetricPlayer>> g_instances;
std::shared_mutex g_instances_mutex;

class InstanceAccess
{
public:
	/// Render events carry the registry token because Unity accepts only int data.
	explicit InstanceAccess(int event_id)
		: m_lock(g_instances_mutex)
	{
		const auto iterator = g_instances.find(event_id);
		if (iterator != g_instances.end())
			m_instance = iterator->second.get();
	}

	explicit InstanceAccess(const OpenVolumetricPlayer* handle)
		: m_lock(g_instances_mutex)
	{
		if (handle == nullptr || handle->magic != kPlayerHandleMagic)
			return;
		const auto iterator = g_instances.find(handle->event_id);
		if (iterator != g_instances.end())
			m_instance = iterator->second.get();
	}

	explicit operator bool() const { return m_instance != nullptr; }
	UnityOpenVolumetricPlayer* operator->() const { return m_instance; }

private:
	std::shared_lock<std::shared_mutex> m_lock;
	UnityOpenVolumetricPlayer* m_instance = nullptr;
};

OpenVolumetricResult classify_error(const std::string& message)
{
	if (message.empty())
		return OPENVOLUMETRIC_RESULT_INTERNAL_FAILURE;
	std::string lower = message;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	if (lower.find("cancel") != std::string::npos)
		return OPENVOLUMETRIC_RESULT_CANCELLED;
	if (lower.find("timeout") != std::string::npos ||
		lower.find("timed out") != std::string::npos)
		return OPENVOLUMETRIC_RESULT_TIMEOUT;
	if (lower.find("http") != std::string::npos ||
		lower.find("network") != std::string::npos ||
		lower.find("connection") != std::string::npos)
		return OPENVOLUMETRIC_RESULT_NETWORK_FAILURE;
	if (lower.find("unsupported") != std::string::npos ||
		lower.find("missing") != std::string::npos)
		return OPENVOLUMETRIC_RESULT_UNSUPPORTED_FORMAT;
	if (lower.find("corrupt") != std::string::npos ||
		lower.find("invalid data") != std::string::npos)
		return OPENVOLUMETRIC_RESULT_CORRUPT_DATA;
	if (lower.find("decode") != std::string::npos ||
		lower.find("codec") != std::string::npos)
		return OPENVOLUMETRIC_RESULT_DECODER_FAILURE;
	return OPENVOLUMETRIC_RESULT_INTERNAL_FAILURE;
}

template<typename T>
bool valid_snapshot(T* value)
{
	return value != nullptr && value->struct_size >= sizeof(T);
}

template<typename T>
bool valid_snapshot(const T* value)
{
	return value != nullptr && value->struct_size >= sizeof(T);
}

template<std::size_t Size>
void copy_string(char (&target)[Size], const std::string& source)
{
	const std::size_t count = std::min(source.size(), Size - 1);
	std::memcpy(target, source.data(), count);
	target[count] = '\0';
}
} // namespace
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);
static IUnityInterfaces* g_unity_interfaces = nullptr;
static IUnityGraphics* g_unity_graphics = nullptr;
extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces * unityInterfaces)
{
	g_unity_interfaces	= unityInterfaces;
	g_unity_graphics			= g_unity_interfaces->Get<IUnityGraphics>();
	g_unity_graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// Unity does not guarantee a separate initialize notification after load.
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	g_unity_graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}
static UnityGfxRenderer g_device_type = kUnityGfxRendererNull;
static void* g_graphics_device = nullptr;
#if defined(_WIN32)
static D3D12UnityContext g_d3d12_context;
static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsD3D11* d3d11 = g_unity_interfaces->Get<IUnityGraphicsD3D11>();
		g_graphics_device = d3d11->GetDevice();
	}
}
static void DoEventGraphicsDeviceD3D12(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		g_d3d12_context.unity =
			g_unity_interfaces->Get<IUnityGraphicsD3D12v8>();
		g_d3d12_context.device = g_d3d12_context.unity == nullptr
			? nullptr : g_d3d12_context.unity->GetDevice();
		g_graphics_device = g_d3d12_context.device == nullptr
			? nullptr : &g_d3d12_context;
	}
	else if (eventType == kUnityGfxDeviceEventShutdown)
	{
		g_d3d12_context = {};
	}
}
#elif defined(__APPLE__)
static void DoEventGraphicsDeviceMetal(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsMetal* metal = g_unity_interfaces->Get<IUnityGraphicsMetal>();
		g_graphics_device = metal;
	}
}
#elif defined(__ANDROID__)
static void DoEventGraphicsDeviceVulkan(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		g_graphics_device =
			g_unity_interfaces->Get<IUnityGraphicsVulkan>();
	}
}
#endif
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	UnityGfxRenderer currentDeviceType = g_device_type;
	
	switch (eventType)
	{
		case kUnityGfxDeviceEventInitialize:
		{
			g_device_type = g_unity_graphics->GetRenderer();
			currentDeviceType = g_device_type;
			break;
		}

		case kUnityGfxDeviceEventShutdown:
			g_device_type = kUnityGfxRendererNull;
			break;

		case kUnityGfxDeviceEventBeforeReset:
			break;

		case kUnityGfxDeviceEventAfterReset:
			break;
	};

	if (currentDeviceType == kUnityGfxRendererD3D11)
	{
#if defined(_WIN32)
		DoEventGraphicsDeviceD3D11(eventType);
#endif
	}
	else if (currentDeviceType == kUnityGfxRendererD3D12)
	{
#if defined(_WIN32)
		DoEventGraphicsDeviceD3D12(eventType);
#endif
	}
	else if (currentDeviceType == kUnityGfxRendererMetal)
	{
#if defined(__APPLE__)
		DoEventGraphicsDeviceMetal(eventType);
#endif
	}
	else if (currentDeviceType == kUnityGfxRendererVulkan)
	{
#if defined(__ANDROID__)
		DoEventGraphicsDeviceVulkan(eventType);
#endif
	}

	if (eventType == kUnityGfxDeviceEventShutdown)
		g_graphics_device = nullptr;
}
/// Unity invokes this on its render thread for GL.IssuePluginEvent. The event
/// identifier selects the player whose complete presentation is uploaded.
static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	if (g_device_type == kUnityGfxRendererNull)
		return;

	InstanceAccess instance(eventID);
	if (!instance)
	{
		LOG("OpenVolumetric render event ignored; token not found: %d", eventID);
		return;
	}

	instance->render();
}
/// Returns the render-thread callback consumed by GL.IssuePluginEvent.
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}
OPENVOLUMETRIC_API void openvolumetric_open_external_console()
{
	Logger::instance()->open_external_console();
}
OPENVOLUMETRIC_API void openvolumetric_close_external_console()
{
	Logger::instance()->close_external_console();
}
OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_get_api_version(
	OpenVolumetricApiVersionV1* version)
{
	if (!valid_snapshot(version))
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	version->major = OPENVOLUMETRIC_ABI_VERSION_MAJOR;
	version->minor = OPENVOLUMETRIC_ABI_VERSION_MINOR;
	version->patch = OPENVOLUMETRIC_ABI_VERSION_PATCH;
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_create(
	OpenVolumetricPlayer** player)
{
	if (player == nullptr)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	*player = nullptr;
	std::unique_lock<std::shared_mutex> lock(g_instances_mutex);
	int event_id = 0;
	while (g_instances.find(event_id) != g_instances.end())
		++event_id;

	std::unique_ptr<UnityOpenVolumetricPlayer> instance;
#if defined(_WIN32)
	if (g_device_type == kUnityGfxRendererD3D11)
		instance = std::make_unique<UnityOpenVolumetricPlayer>(
			event_id,
			std::make_unique<TextureD3D11>(),
			std::make_unique<MeshBufferD3D11>());
	else if (g_device_type == kUnityGfxRendererD3D12)
		instance = std::make_unique<UnityOpenVolumetricPlayer>(
			event_id,
			std::make_unique<TextureD3D12>(),
			std::make_unique<MeshBufferD3D12>());
#elif defined(__APPLE__)
	if (g_device_type == kUnityGfxRendererMetal)
		instance = std::make_unique<UnityOpenVolumetricPlayer>(
			event_id,
			std::make_unique<TextureMetal>(),
			std::make_unique<MeshBufferMetal>());
#elif defined(__ANDROID__)
	if (g_device_type == kUnityGfxRendererVulkan)
		instance = std::make_unique<UnityOpenVolumetricPlayer>(
			event_id,
			std::make_unique<TextureVulkan>(),
			std::make_unique<MeshBufferVulkan>());
#endif
	if (!instance || g_graphics_device == nullptr)
		return OPENVOLUMETRIC_RESULT_UNSUPPORTED_FORMAT;

	auto handle = std::make_unique<OpenVolumetricPlayer>();
	handle->magic = kPlayerHandleMagic;
	handle->event_id = event_id;
	g_instances.emplace(event_id, std::move(instance));
	*player = handle.release();
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_destroy(
	OpenVolumetricPlayer* player)
{
	if (player == nullptr || player->magic != kPlayerHandleMagic)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	std::unique_lock<std::shared_mutex> lock(g_instances_mutex);
	const auto iterator = g_instances.find(player->event_id);
	if (iterator == g_instances.end())
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	openvolumetric::unity::stop_dsp_audio(iterator->second.get());
	g_instances.erase(iterator);
	player->magic = 0;
	delete player;
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_render_event_id(
	OpenVolumetricPlayer* player, int32_t* event_id)
{
	if (event_id == nullptr)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	*event_id = player->event_id;
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_set_time(
	OpenVolumetricPlayer* player, double time)
{
	if (!std::isfinite(time))
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	instance->set_presentation_time(time);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_start(
	OpenVolumetricPlayer* player)
{
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	return instance->start() ? OPENVOLUMETRIC_RESULT_OK
		: OPENVOLUMETRIC_RESULT_DECODER_FAILURE;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_stop(
	OpenVolumetricPlayer* player)
{
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	return instance->stop() ? OPENVOLUMETRIC_RESULT_OK
		: OPENVOLUMETRIC_RESULT_DECODER_FAILURE;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_seek(
	OpenVolumetricPlayer* player, double time)
{
	if (!std::isfinite(time) || time < 0.0)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	return instance->seek(time) ? OPENVOLUMETRIC_RESULT_OK
		: classify_error(instance->error());
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_open(
	OpenVolumetricPlayer* player, const char* resource)
{
	if (resource == nullptr || resource[0] == '\0')
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	return instance->open(resource) ? OPENVOLUMETRIC_RESULT_OK
		: classify_error(instance->error());
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_error(
	OpenVolumetricPlayer* player, char* buffer, uint32_t capacity,
	uint32_t* required_capacity, OpenVolumetricResult* category)
{
	if (required_capacity == nullptr || category == nullptr)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	const std::string error = instance->error();
	*category = error.empty() ? OPENVOLUMETRIC_RESULT_OK : classify_error(error);
	*required_capacity = static_cast<uint32_t>(error.size() + 1);
	if (buffer == nullptr && capacity == 0)
		return OPENVOLUMETRIC_RESULT_OK;
	if (buffer == nullptr || capacity < *required_capacity)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	std::memcpy(buffer, error.c_str(), *required_capacity);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_centroid(
	OpenVolumetricPlayer* player, OpenVolumetricCentroidV1* centroid)
{
	if (!valid_snapshot(centroid))
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	return instance->geometry_centroid(centroid->x, centroid->y, centroid->z)
		? OPENVOLUMETRIC_RESULT_OK : OPENVOLUMETRIC_RESULT_NOT_READY;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_adaptive_select(
	const OpenVolumetricAdaptiveSelectionRequestV1* request,
	OpenVolumetricAdaptiveSelectionV1* output,
	OpenVolumetricAdaptiveRepresentationV1* representations,
	uint32_t representation_capacity)
{
	if (!valid_snapshot(request) || !valid_snapshot(output) ||
		request->manifest_location == nullptr || request->quality < 0 ||
		request->quality > 2 ||
		(representation_capacity > 0 && representations == nullptr))
	{
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	}
	openvolumetric::AdaptiveCapabilityLimits limits;
	limits.maximum_texture_width = request->maximum_texture_width;
	limits.maximum_texture_height = request->maximum_texture_height;
	limits.maximum_texture_bitrate = request->maximum_texture_bitrate;
	limits.maximum_geometry_bitrate = request->maximum_geometry_bitrate;
	limits.maximum_bandwidth = request->maximum_bandwidth;
	openvolumetric::AdaptiveSelection selection;
	std::string error;
	const auto quality =
		static_cast<openvolumetric::AdaptiveQuality>(request->quality);
	const bool selected = request->manifest_json != nullptr
		? openvolumetric::select_adaptive_representation(
			request->manifest_json, request->manifest_location, quality,
			limits, selection, error)
		: openvolumetric::load_adaptive_representation(
			request->manifest_location, quality, limits, selection, error);
	copy_string(output->error, error);
	if (!selected)
		return classify_error(error);

	output->measured_throughput_bits_per_second =
		selection.measured_throughput_bps;
	output->representation_count = static_cast<uint32_t>(
		selection.eligible_representations.size());
	output->segment_duration = selection.manifest.segment_duration_seconds;
	copy_string(output->representation_id, selection.representation.id);
	copy_string(output->resource_uri, selection.representation.resource_uri);
	copy_string(output->resolved_resource, selection.resolved_resource);
	copy_string(output->decision_reason, selection.decision_reason);
	const uint32_t count = std::min(
		output->representation_count, representation_capacity);
	for (uint32_t index = 0; index < count; ++index)
	{
		auto& target = representations[index];
		if (target.struct_size < sizeof(target))
			return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
		const auto& source = selection.eligible_representations[index];
		target.bandwidth = source.representation.bandwidth;
		copy_string(target.id, source.representation.id);
		copy_string(target.resource, source.resolved_resource);
	}
	return output->representation_count <= representation_capacity
		? OPENVOLUMETRIC_RESULT_OK
		: OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_configure_adaptive(
	OpenVolumetricPlayer* player, const char* representation_id)
{
	if (representation_id == nullptr)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	instance->set_active_representation_id(representation_id);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_clear_adaptive_policy(OpenVolumetricPlayer* player)
{
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	instance->clear_adaptive_policy();
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_add_adaptive_representation(
	OpenVolumetricPlayer* player,
	const char* representation_id,
	const char* resource,
	uint64_t bandwidth)
{
	if (representation_id == nullptr || resource == nullptr || bandwidth == 0)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	instance->add_adaptive_policy_representation(
		representation_id, resource, bandwidth);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_update_adaptive_policy(
	OpenVolumetricPlayer* player,
	double now,
	double presentation_time,
	double duration,
	double segment_duration,
	int32_t* action)
{
	if (action == nullptr)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	openvolumetric::AdaptivePolicyObservation observation;
	observation.now = now;
	observation.presentation_time = presentation_time;
	observation.duration = duration;
	observation.segment_duration = segment_duration;
	*action = static_cast<int32_t>(
		instance->update_adaptive_policy(observation).action);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_request_adaptive_index(
	OpenVolumetricPlayer* player,
	uint64_t target_index,
	double now,
	double presentation_time,
	double duration,
	double segment_duration,
	int32_t* action)
{
	if (action == nullptr)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	openvolumetric::AdaptivePolicyObservation observation;
	observation.now = now;
	observation.presentation_time = presentation_time;
	observation.duration = duration;
	observation.segment_duration = segment_duration;
	*action = static_cast<int32_t>(instance->request_adaptive_policy(
		static_cast<std::size_t>(target_index), observation).action);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_get_media_info(
	OpenVolumetricPlayer* player, OpenVolumetricMediaInfoV1* output)
{
	if (!valid_snapshot(output))
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	const openvolumetric::OpenVolumetricMediaInfo info = instance->media_info();
	output->width = info.width;
	output->height = info.height;
	output->frame_rate = info.frame_rate;
	output->duration = info.duration;
	output->has_audio = info.has_audio ? 1 : 0;
	output->audio_sample_rate = info.audio_sample_rate;
	output->audio_channels = info.audio_channels;
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_get_runtime_snapshot(
	OpenVolumetricPlayer* player, OpenVolumetricRuntimeSnapshotV1* output)
{
	if (!valid_snapshot(output))
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	const auto buffer = instance->buffer_info();
	const auto audio = instance->audio_buffer_info();
	output->input_state = static_cast<int32_t>(buffer.state);
	output->remote = buffer.remote ? 1 : 0;
	output->resource_size_bytes = buffer.resource_size_bytes;
	output->cached_bytes = buffer.cached_bytes;
	output->downloaded_bytes = buffer.downloaded_bytes;
	output->transfer_throughput_bits_per_second =
		buffer.transfer_throughput_bits_per_second;
	output->request_count = buffer.request_count;
	output->recovery_count = buffer.recovery_count;
	output->fragmented = buffer.fragmented ? 1 : 0;
	output->active_fragment = buffer.active_fragment;
	output->fragment_count = buffer.fragment_count;
	output->cached_fragment_count = buffer.cached_fragment_count;
	output->audio_read_time = audio.read_time;
	output->audio_buffered_duration = audio.buffered_duration;
	output->audio_underrun_count = audio.underrun_count;
	output->last_presented_time = instance->last_presented_time();
	output->adaptive_policy_throughput_bits_per_second =
		instance->adaptive_policy_throughput();
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_get_adaptive_switch_snapshot(
	OpenVolumetricPlayer* player,
	OpenVolumetricAdaptiveSwitchSnapshotV1* output)
{
	if (!valid_snapshot(output))
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	const auto info = instance->adaptive_switch_info();
	output->state = static_cast<int32_t>(info.state);
	output->generation = info.generation;
	output->switch_count = info.switch_count;
	output->boundary_time = info.boundary_time;
	copy_string(output->active_representation, info.active_representation);
	copy_string(output->pending_representation, info.pending_representation);
	copy_string(output->reason, info.reason);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_schedule_dsp_audio(
	OpenVolumetricPlayer* player,
	uint64_t dsp_start_tick,
	double media_start_time)
{
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	openvolumetric::unity::schedule_dsp_audio(
		instance.operator->(), dsp_start_tick, media_start_time);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_stop_dsp_audio(
	OpenVolumetricPlayer* player)
{
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	openvolumetric::unity::stop_dsp_audio(instance.operator->());
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API double openvolumetric_get_dsp_audio_time()
{
	return openvolumetric::unity::dsp_audio_time();
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_get_texture_pointers(
	OpenVolumetricPlayer* player, void** y, void** u, void** v)
{
	if (y == nullptr || u == nullptr || v == nullptr)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;

	const openvolumetric::OpenVolumetricMediaInfo& info =
		instance->media_info();
	const int width = info.width;
	const int height = info.height;
	const int ret =
		instance->texture()->init(g_graphics_device, width, height);
	if (ret == -1)
		return OPENVOLUMETRIC_RESULT_INTERNAL_FAILURE;

	instance->texture()->get_resource_pointers(
		*y, *u, *v);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult
openvolumetric_player_register_texture_pointers(
	OpenVolumetricPlayer* player, void* y, void* u, void* v)
{
	InstanceAccess instance(player);
	if (!instance ||
		y == nullptr || u == nullptr || v == nullptr)
		return instance ? OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT
			: OPENVOLUMETRIC_RESULT_INVALID_HANDLE;

	instance->texture()->register_resource_pointers(
		y, u, v);
	return OPENVOLUMETRIC_RESULT_OK;
}

OPENVOLUMETRIC_API OpenVolumetricResult openvolumetric_player_set_mesh_buffers(
	OpenVolumetricPlayer* player, void* index_buffer_handle,
	int32_t index_count, void* vertex_buffer_handle, int32_t vertex_count)
{
	if (index_buffer_handle == nullptr || vertex_buffer_handle == nullptr ||
		index_count <= 0 || vertex_count <= 0)
		return OPENVOLUMETRIC_RESULT_INVALID_ARGUMENT;
	InstanceAccess instance(player);
	if (!instance)
		return OPENVOLUMETRIC_RESULT_INVALID_HANDLE;
	if (!instance->mesh_buffer()->init(
		g_graphics_device,
		index_buffer_handle,
		index_count,
		vertex_buffer_handle,
		vertex_count))
		return OPENVOLUMETRIC_RESULT_INTERNAL_FAILURE;
	return OPENVOLUMETRIC_RESULT_OK;
}
 
