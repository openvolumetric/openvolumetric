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

OPENVOLUMETRIC_API int openvolumetric_read_audio(
	int id, float* samples, int sample_count)
{
	InstanceAccess instance(id);
	if (!instance || samples == nullptr || sample_count <= 0)
		return -1;

	return instance->read_audio(samples, sample_count);
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
 
