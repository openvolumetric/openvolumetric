#include "openvolumetric-unity-api.h"

#if defined(_WIN32)
#include <d3d11.h>
#endif

#include <Unity/IUnityInterface.h>
#include <Unity/IUnityGraphics.h>
#if defined(_WIN32)
#include <Unity/IUnityGraphicsD3D11.h>
#elif defined(__APPLE__)
#include <Unity/IUnityGraphicsMetal.h>
#import <Metal/Metal.h>
#elif defined(__ANDROID__)
#include <Unity/IUnityGraphicsVulkan.h>
#endif

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#include <MeshBufferD3D11.h>
#include <TextureD3D11.h>
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
#elif defined(__APPLE__)
using openvolumetric::unity::MeshBufferMetal;
using openvolumetric::unity::TextureMetal;
#elif defined(__ANDROID__)
using openvolumetric::unity::MeshBufferVulkan;
using openvolumetric::unity::TextureVulkan;
#endif
namespace
{
/// Owns all live Unity players. Normal API/render calls retain a shared lock
/// for the complete operation; destruction takes the exclusive lock, so a
/// queued render event cannot use an instance while it is being deleted.
std::unordered_map<int, std::unique_ptr<UnityOpenVolumetricPlayer>> g_instances;
std::shared_mutex g_instances_mutex;
thread_local openvolumetric::AdaptiveSelection g_adaptive_selection;
thread_local std::string g_adaptive_error;
thread_local openvolumetric::AdaptiveSwitchInfo g_adaptive_switch_info;

class InstanceAccess
{
public:
	explicit InstanceAccess(int id)
		: m_lock(g_instances_mutex)
	{
		const auto iterator = g_instances.find(id);
		if (iterator != g_instances.end())
			m_instance = iterator->second.get();
	}

	explicit operator bool() const { return m_instance != nullptr; }
	UnityOpenVolumetricPlayer* operator->() const { return m_instance; }

private:
	std::shared_lock<std::shared_mutex> m_lock;
	UnityOpenVolumetricPlayer* m_instance = nullptr;
};
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
static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsD3D11* d3d11 = g_unity_interfaces->Get<IUnityGraphicsD3D11>();
		g_graphics_device = d3d11->GetDevice();
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
		LOG("openvolumetric_quit - cannot find instance id: %d", eventID);
		return;
	}

	instance->render();
}
/// Returns the render-thread callback consumed by GL.IssuePluginEvent.
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}
OPENVOLUMETRIC_API void	openvolumetric_open_external_console()
{
	Logger::instance()->open_external_console();
}
OPENVOLUMETRIC_API void	openvolumetric_close_external_console()
{
	Logger::instance()->close_external_console();
}
/// Creates one player and returns its registry identifier through `id`.
OPENVOLUMETRIC_API int openvolumetric_init(int& id)
{
	LOG("openvolumetric_init - start");

	std::unique_lock<std::shared_mutex> lock(g_instances_mutex);
	id = 0;
	while (g_instances.find(id) != g_instances.end())
		++id;
	
	LOG("openvolumetric_init - id: %d", id);

	std::unique_ptr<UnityOpenVolumetricPlayer> instance;
#if defined(_WIN32)
	if (g_device_type == kUnityGfxRendererD3D11)
		instance = std::make_unique<UnityOpenVolumetricPlayer>(
			id,
			std::make_unique<TextureD3D11>(),
			std::make_unique<MeshBufferD3D11>());
#elif defined(__APPLE__)
	if (g_device_type == kUnityGfxRendererMetal)
		instance = std::make_unique<UnityOpenVolumetricPlayer>(
			id,
			std::make_unique<TextureMetal>(),
			std::make_unique<MeshBufferMetal>());
#elif defined(__ANDROID__)
	if (g_device_type == kUnityGfxRendererVulkan)
		instance = std::make_unique<UnityOpenVolumetricPlayer>(
			id,
			std::make_unique<TextureVulkan>(),
			std::make_unique<MeshBufferVulkan>());
#endif
	if (!instance || g_graphics_device == nullptr)
	{
		LOG("openvolumetric_init - unsupported or unavailable graphics device: %d", g_device_type);
		return -1;
	}

	g_instances.emplace(id, std::move(instance));

	LOG("openvolumetric_init - end");

	return 1;
}
OPENVOLUMETRIC_API void	openvolumetric_quit(int id)
{
	LOG("openvolumetric_quit - id: %d",id);

	std::unique_lock<std::shared_mutex> lock(g_instances_mutex);
	const auto iterator = g_instances.find(id);
	if (iterator == g_instances.end())
	{
		LOG("openvolumetric_quit - cannot find instance id: %d", id);
		return;
	}

	openvolumetric::unity::stop_dsp_audio(iterator->second.get());
	g_instances.erase(iterator);
}

OPENVOLUMETRIC_API void openvolumetric_set_time(int id, double time)
{
	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_set_time - cannot find instance id: %d", id);
		return;
	}

	instance->set_presentation_time(time);
}
OPENVOLUMETRIC_API int	openvolumetric_start_decoding(int id)
{
	LOG("openvolumetric_start_decoding - id: %d", id);

	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_start_decoding - cannot find instance id: %d", id);
		return -1;
	}

	return instance->start() ? 1 : -1;
}

OPENVOLUMETRIC_API int	openvolumetric_stop_decoding(int id)
{
	LOG("openvolumetric_stop_decoding - id: %d", id);

	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_stop_decoding - cannot find instance id: %d", id);
		return -1;
	}

	return instance->stop() ? 1 : -1;

}
OPENVOLUMETRIC_API int	openvolumetric_seek(int id, double time)
{
	LOG("openvolumetric_seek - id: %d - time: %f", id, time);

	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_seek - cannot find instance id: %d", id);
		return -1;
	}

	return instance->seek(time) ? 1 : -1;
}
OPENVOLUMETRIC_API int	openvolumetric_load_video(int id, const char* filepath)
{
	LOG("openvolumetric_load_video - id: %d", id);

	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_load_video - cannot find instance id: %d", id);
		return -1;
	}

	if (!instance->open(filepath))
		return -1;

	return 1;
}

OPENVOLUMETRIC_API const char* openvolumetric_get_last_error(int id)
{
	static thread_local std::string error;
	InstanceAccess instance(id);
	if (!instance)
	{
		error = "Volumetric video instance was not found.";
		return error.c_str();
	}
	error = instance->error();
	return error.c_str();
}

OPENVOLUMETRIC_API double openvolumetric_get_last_presented_time(int id)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1.0;
	return instance->last_presented_time();
}

OPENVOLUMETRIC_API int openvolumetric_get_geometry_centroid(
	int id,
	float& x,
	float& y,
	float& z)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1;
	return instance->geometry_centroid(x, y, z) ? 1 : 0;
}

OPENVOLUMETRIC_API int openvolumetric_get_buffer_details(
	int id,
	int& state,
	int& remote,
	long long& resource_size_bytes,
	unsigned long long& cached_bytes,
	unsigned long long& downloaded_bytes,
	unsigned long long& request_count,
	unsigned long long& recovery_count)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1;
	const openvolumetric::OpenVolumetricBufferInfo info =
		instance->buffer_info();
	state = static_cast<int>(info.state);
	remote = info.remote ? 1 : 0;
	resource_size_bytes = static_cast<long long>(info.resource_size_bytes);
	cached_bytes = static_cast<unsigned long long>(info.cached_bytes);
	downloaded_bytes =
		static_cast<unsigned long long>(info.downloaded_bytes);
	request_count = static_cast<unsigned long long>(info.request_count);
	recovery_count = static_cast<unsigned long long>(info.recovery_count);
	return 1;
}

OPENVOLUMETRIC_API int openvolumetric_get_fragment_details(
	int id,
	int& fragmented,
	long long& active_fragment,
	unsigned long long& fragment_count,
	unsigned long long& cached_fragment_count)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1;
	const openvolumetric::OpenVolumetricBufferInfo info =
		instance->buffer_info();
	fragmented = info.fragmented ? 1 : 0;
	active_fragment = static_cast<long long>(info.active_fragment);
	fragment_count = static_cast<unsigned long long>(info.fragment_count);
	cached_fragment_count =
		static_cast<unsigned long long>(info.cached_fragment_count);
	return 1;
}

OPENVOLUMETRIC_API int openvolumetric_select_adaptive_representation(
	const char* manifest_json,
	const char* manifest_location,
	int quality)
{
	g_adaptive_selection = {};
	g_adaptive_error.clear();
	if (manifest_json == nullptr || manifest_location == nullptr ||
		quality < 0 || quality > 2)
	{
		g_adaptive_error = "Adaptive manifest input or quality is invalid.";
		return -1;
	}
	return openvolumetric::select_adaptive_representation(
		manifest_json,
		manifest_location,
		static_cast<openvolumetric::AdaptiveQuality>(quality),
		g_adaptive_selection,
		g_adaptive_error)
		? 1
		: -1;
}

OPENVOLUMETRIC_API int openvolumetric_load_adaptive_representation(
	const char* manifest_location,
	int quality)
{
	g_adaptive_selection = {};
	g_adaptive_error.clear();
	if (manifest_location == nullptr || quality < 0 || quality > 2)
	{
		g_adaptive_error = "Adaptive manifest location or quality is invalid.";
		return -1;
	}
	return openvolumetric::load_adaptive_representation(
		manifest_location,
		static_cast<openvolumetric::AdaptiveQuality>(quality),
		g_adaptive_selection,
		g_adaptive_error)
		? 1
		: -1;
}

OPENVOLUMETRIC_API int openvolumetric_load_adaptive_representation_with_capabilities(
	const char* manifest_location,
	int quality,
	unsigned int maximum_texture_width,
	unsigned int maximum_texture_height,
	unsigned long long maximum_texture_bitrate,
	unsigned long long maximum_geometry_bitrate,
	unsigned long long maximum_bandwidth)
{
	g_adaptive_selection = {};
	g_adaptive_error.clear();
	if (manifest_location == nullptr || quality < 0 || quality > 2)
	{
		g_adaptive_error = "Adaptive manifest location or quality is invalid.";
		return -1;
	}
	openvolumetric::AdaptiveCapabilityLimits limits;
	limits.maximum_texture_width = maximum_texture_width;
	limits.maximum_texture_height = maximum_texture_height;
	limits.maximum_texture_bitrate = maximum_texture_bitrate;
	limits.maximum_geometry_bitrate = maximum_geometry_bitrate;
	limits.maximum_bandwidth = maximum_bandwidth;
	return openvolumetric::load_adaptive_representation(
		manifest_location,
		static_cast<openvolumetric::AdaptiveQuality>(quality),
		limits,
		g_adaptive_selection,
		g_adaptive_error)
		? 1
		: -1;
}

OPENVOLUMETRIC_API int openvolumetric_select_adaptive_representation_with_capabilities(
	const char* manifest_json,
	const char* manifest_location,
	int quality,
	unsigned int maximum_texture_width,
	unsigned int maximum_texture_height,
	unsigned long long maximum_texture_bitrate,
	unsigned long long maximum_geometry_bitrate,
	unsigned long long maximum_bandwidth)
{
	g_adaptive_selection = {};
	g_adaptive_error.clear();
	if (manifest_json == nullptr || manifest_location == nullptr ||
		quality < 0 || quality > 2)
	{
		g_adaptive_error = "Adaptive manifest input or quality is invalid.";
		return -1;
	}
	openvolumetric::AdaptiveCapabilityLimits limits;
	limits.maximum_texture_width = maximum_texture_width;
	limits.maximum_texture_height = maximum_texture_height;
	limits.maximum_texture_bitrate = maximum_texture_bitrate;
	limits.maximum_geometry_bitrate = maximum_geometry_bitrate;
	limits.maximum_bandwidth = maximum_bandwidth;
	return openvolumetric::select_adaptive_representation(
		manifest_json,
		manifest_location,
		static_cast<openvolumetric::AdaptiveQuality>(quality),
		limits,
		g_adaptive_selection,
		g_adaptive_error)
		? 1
		: -1;
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_resource_uri()
{
	return g_adaptive_selection.representation.resource_uri.c_str();
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_resource()
{
	return g_adaptive_selection.resolved_resource.c_str();
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_representation_id()
{
	return g_adaptive_selection.representation.id.c_str();
}

OPENVOLUMETRIC_API unsigned long long openvolumetric_get_adaptive_throughput_bps()
{
	return static_cast<unsigned long long>(
		g_adaptive_selection.measured_throughput_bps);
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_decision_reason()
{
	return g_adaptive_selection.decision_reason.c_str();
}

OPENVOLUMETRIC_API unsigned long long openvolumetric_get_adaptive_representation_count()
{
	return static_cast<unsigned long long>(
		g_adaptive_selection.eligible_representations.size());
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_representation_id_at(
	unsigned long long index)
{
	const auto& representations = g_adaptive_selection.eligible_representations;
	return index < representations.size()
		? representations[static_cast<std::size_t>(index)].representation.id.c_str()
		: "";
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_resource_at(
	unsigned long long index)
{
	const auto& representations = g_adaptive_selection.eligible_representations;
	return index < representations.size()
		? representations[static_cast<std::size_t>(index)].resolved_resource.c_str()
		: "";
}

OPENVOLUMETRIC_API unsigned long long openvolumetric_get_adaptive_bandwidth_at(
	unsigned long long index)
{
	const auto& representations = g_adaptive_selection.eligible_representations;
	return index < representations.size()
		? representations[static_cast<std::size_t>(index)].representation.bandwidth
		: 0;
}

OPENVOLUMETRIC_API double openvolumetric_get_adaptive_segment_duration()
{
	return g_adaptive_selection.manifest.segment_duration_seconds;
}

OPENVOLUMETRIC_API int openvolumetric_configure_adaptive_instance(
	int id,
	const char* representation_id)
{
	InstanceAccess instance(id);
	if (!instance || representation_id == nullptr)
		return -1;
	instance->set_active_representation_id(representation_id);
	return 1;
}

OPENVOLUMETRIC_API int openvolumetric_request_adaptive_switch(
	int id,
	const char* resource,
	const char* representation_id,
	double boundary_time,
	const char* reason)
{
	InstanceAccess instance(id);
	return instance && instance->request_adaptive_switch(
		resource, representation_id, boundary_time, reason)
		? 1
		: -1;
}

OPENVOLUMETRIC_API void openvolumetric_cancel_adaptive_switch(int id)
{
	InstanceAccess instance(id);
	if (instance)
		instance->cancel_adaptive_switch();
}

OPENVOLUMETRIC_API int openvolumetric_get_adaptive_switch_details(
	int id,
	int& state,
	unsigned long long& generation,
	unsigned long long& switch_count,
	double& boundary_time)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1;
	g_adaptive_switch_info = instance->adaptive_switch_info();
	state = static_cast<int>(g_adaptive_switch_info.state);
	generation = g_adaptive_switch_info.generation;
	switch_count = g_adaptive_switch_info.switch_count;
	boundary_time = g_adaptive_switch_info.boundary_time;
	return 1;
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_switch_active_id()
{
	return g_adaptive_switch_info.active_representation.c_str();
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_switch_pending_id()
{
	return g_adaptive_switch_info.pending_representation.c_str();
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_switch_reason()
{
	return g_adaptive_switch_info.reason.c_str();
}

OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_error()
{
	return g_adaptive_error.c_str();
}

OPENVOLUMETRIC_API int	openvolumetric_get_video_details(int id, int& width, int& height, double& fps, double& duration)
{
	LOG("openvolumetric_get_video_details - id: %d", id);

	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_get_video_details - cannot find instance id: %d", id);
		return -1;
	}

	const openvolumetric::OpenVolumetricMediaInfo& info =
		instance->media_info();
	width = info.width;
	height = info.height;
	fps = info.frame_rate;
	duration = info.duration;

	return 1;
}

OPENVOLUMETRIC_API int openvolumetric_get_audio_details(
	int id, int& sample_rate, int& channels)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1;

	const openvolumetric::OpenVolumetricMediaInfo& info =
		instance->media_info();
	if (!info.has_audio)
	{
		sample_rate = 0;
		channels = 0;
		return 0;
	}

	sample_rate = info.audio_sample_rate;
	channels = info.audio_channels;
	return 1;
}

OPENVOLUMETRIC_API int openvolumetric_schedule_dsp_audio(
	int id,
	unsigned long long dsp_start_tick,
	double media_start_time)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1;
	openvolumetric::unity::schedule_dsp_audio(
		instance.operator->(),
		static_cast<std::uint64_t>(dsp_start_tick),
		media_start_time);
	return 1;
}

OPENVOLUMETRIC_API void openvolumetric_stop_dsp_audio(int id)
{
	InstanceAccess instance(id);
	if (instance)
		openvolumetric::unity::stop_dsp_audio(instance.operator->());
}

OPENVOLUMETRIC_API double openvolumetric_get_dsp_audio_time()
{
	return openvolumetric::unity::dsp_audio_time();
}

OPENVOLUMETRIC_API int openvolumetric_get_audio_buffer_details(
	int id,
	double& read_time,
	double& buffered_duration,
	unsigned long long& underrun_count)
{
	InstanceAccess instance(id);
	if (!instance)
		return -1;

	const openvolumetric::OpenVolumetricAudioBufferInfo info =
		instance->audio_buffer_info();
	read_time = info.read_time;
	buffered_duration = info.buffered_duration;
	underrun_count =
		static_cast<unsigned long long>(info.underrun_count);
	return 1;
}
OPENVOLUMETRIC_API int	openvolumetric_get_texture_pointers(int id, void*& yPointer, void*& uPointer, void*& vPointer)
{
	LOG("openvolumetric_set_texture_pointer - id: %d", id);

	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_get_texture_pointers - cannot find instance id: %d", id);
		return -1;
	}

	const openvolumetric::OpenVolumetricMediaInfo& info =
		instance->media_info();
	const int width = info.width;
	const int height = info.height;
	LOG("openvolumetric_set_texture_pointer - %d x %d", width, height);

	const int ret =
		instance->texture()->init(g_graphics_device, width, height);
	if (ret == -1)
	{
		return -1;
	}

	instance->texture()->getResourcePointers(
		yPointer, uPointer, vPointer);

	LOG("openvolumetric_set_texture_pointer - end");
	return 1;	
}

OPENVOLUMETRIC_API int openvolumetric_register_texture_pointers(
	int id, void* yPointer, void* uPointer, void* vPointer)
{
	InstanceAccess instance(id);
	if (!instance ||
		yPointer == nullptr || uPointer == nullptr || vPointer == nullptr)
		return -1;

	instance->texture()->registerResourcePointers(
		yPointer, uPointer, vPointer);
	return 1;
}




OPENVOLUMETRIC_API int	openvolumetric_set_mesh_pointer(int id, void* index_buffer_handle, int index_count, void* vertex_buffer_handle, int vertex_count)
{
	LOG("openvolumetric_set_mesh_pointer - id: %d", id);

	InstanceAccess instance(id);
	if (!instance)
	{
		LOG("openvolumetric_get_texture_pointers - cannot find instance id: %d", id);
		return -1;
	}
	
	if (!instance->mesh_buffer()->init(
		g_graphics_device,
		index_buffer_handle,
		index_count,
		vertex_buffer_handle,
		vertex_count))
	{
		return -1;
	}


	return 1;
}
 
