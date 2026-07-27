#include "unity-volumetric-video-api.h"

#if defined(_WIN32)
#include <d3d11.h>
#endif

// Unity Interface
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

// STL + C
#include <assert.h>
#include <list>
#include <iostream>
#include <string>

//
#if defined(_WIN32)
#include <VolumetricVideoD3D11.h>
#elif defined(__APPLE__)
#include <VolumetricVideoMetal.h>
#elif defined(__ANDROID__)
#include <VolumetricVideoVulkan.h>
#endif
#include <IVolumetricVideo.h>
#include <Logger.h>


// --------------------------------------------------------------------------
// Unity functions
// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// Global variables for managing plugin instances
//
std::list<IVolumetricVideo*> vv_instances;
typedef std::list<IVolumetricVideo*>::iterator VolumetricVideo_iter;

// --------------------------------------------------------------------------
// Function to manage the instances
//
bool get_vv_instance(int id, VolumetricVideo_iter* iter)
{
	for (VolumetricVideo_iter it = vv_instances.begin(); it != vv_instances.end(); it++)
	{
		if ((*it)->get_id() == id)
		{
			*iter = it;
			return true;
		}
	}

	// Cant find an instace
	return false;
}


// --------------------------------------------------------------------------
// UnitySetInterface Functions
// --------------------------------------------------------------------------


// --------------------------------------------------------------------------
//
//
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

// --------------------------------------------------------------------------
//
//
static IUnityInterfaces* s_UnityInterfaces = NULL;

// --------------------------------------------------------------------------
//
//
static IUnityGraphics* s_Graphics = NULL;

// --------------------------------------------------------------------------
//
//
extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces * unityInterfaces)
{
	s_UnityInterfaces	= unityInterfaces;
	s_Graphics			= s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

// --------------------------------------------------------------------------
//
//
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

// --------------------------------------------------------------------------
// GraphicsDeviceEvent
//
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

// --------------------------------------------------------------------------
//
//
static void* g_GraphicsDevice = NULL;


// --------------------------------------------------------------------------
//
//
#if defined(_WIN32)
static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsD3D11* d3d11 = s_UnityInterfaces->Get<IUnityGraphicsD3D11>();
		g_GraphicsDevice = d3d11->GetDevice();
	}
}
#elif defined(__APPLE__)
static void DoEventGraphicsDeviceMetal(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsMetal* metal = s_UnityInterfaces->Get<IUnityGraphicsMetal>();
		g_GraphicsDevice = metal;
	}
}
#elif defined(__ANDROID__)
static void DoEventGraphicsDeviceVulkan(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		g_GraphicsDevice =
			s_UnityInterfaces->Get<IUnityGraphicsVulkan>();
	}
}
#endif

// --------------------------------------------------------------------------
//
//
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	//
	UnityGfxRenderer currentDeviceType = s_DeviceType;
	
	//
	switch (eventType)
	{
		case kUnityGfxDeviceEventInitialize:
		{
			s_DeviceType = s_Graphics->GetRenderer();
			currentDeviceType = s_DeviceType;
			break;
		}

		case kUnityGfxDeviceEventShutdown:
			s_DeviceType = kUnityGfxRendererNull;
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
		g_GraphicsDevice = NULL;
}



// --------------------------------------------------------------------------
// OnRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.
//
static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	// Unknown / unsupported graphics device type? Do nothing
	if (s_DeviceType == kUnityGfxRendererNull)
		return;

	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(eventID, &iter))
	{
		LOG("volumetricvideo_quit - cant find instance id: %d", eventID);
		return;
	}

	//
	(*iter)->render();
}



// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.
//
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}


// --------------------------------------------------------------------------
// General functions
// --------------------------------------------------------------------------



// --------------------------------------------------------------------------
// Open external console to see c++ debug  info
//
VOLUMETRIC_VIDEO_API void	volumetricvideo_open_external_console()
{
	Logger::instance()->open_external_console();
}

// --------------------------------------------------------------------------
// Close external console to see c++ debug  info
//
VOLUMETRIC_VIDEO_API void	volumetricvideo_close_external_console()
{
	Logger::instance()->close_external_console();
}


// --------------------------------------------------------------------------
// volumetricvideo_init - init plugin - returns instance id to access other functions
//
VOLUMETRIC_VIDEO_API int volumetricvideo_init(int& ID)
{
	LOG("volumetricvideo_init - start");

	// get instance id
	ID = 0;
	VolumetricVideo_iter iter;
	while (get_vv_instance(ID, &iter))
	{
		ID++;
	}
	
	// Report Instance ID
	LOG("volumetricvideo_init - id: %d", ID);

	IVolumetricVideo* vv = NULL;
#if defined(_WIN32)
	if (s_DeviceType == kUnityGfxRendererD3D11)
		vv = new VolumetricVideoD3D11(ID);
#elif defined(__APPLE__)
	if (s_DeviceType == kUnityGfxRendererMetal)
		vv = new VolumetricVideoMetal(ID);
#elif defined(__ANDROID__)
	if (s_DeviceType == kUnityGfxRendererVulkan)
		vv = new VolumetricVideoVulkan(ID);
#endif
	if (vv == NULL || g_GraphicsDevice == NULL)
	{
		delete vv;
		LOG("volumetricvideo_init - unsupported or unavailable graphics device: %d", s_DeviceType);
		return -1;
	}

	// Add to instance list
	vv_instances.push_back(vv);

	// Complete
	LOG("volumetricvideo_init - end");

	// Done :)	
	return 1;
}

// --------------------------------------------------------------------------
// Quit application
//
VOLUMETRIC_VIDEO_API void	volumetricvideo_quit(int ID)
{
	LOG("volumetricvideo_quit - id: %d",ID);

	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_quit - cant find instance id: %d", ID);
		return;
	}

	// Destroy volumetric video decoder - free up resources
	(*iter)->destroy();

	// delete contents of pointer and erase from list
	delete (*iter);
	vv_instances.erase(iter);
}


// --------------------------------------------------------------------------
// Set presentation target from the engine playback clock.
// --------------------------------------------------------------------------
VOLUMETRIC_VIDEO_API void volumetricvideo_set_time(int ID, double time)
{
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_set_time - cant find instance id: %d", ID);
		return;
	}

	(*iter)->set_presentation_time(time);
}




// --------------------------------------------------------------------------
// Decoder functions
// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// Start decoder 
// --------------------------------------------------------------------------

VOLUMETRIC_VIDEO_API int	volumetricvideo_start_decoding(int ID)
{
	LOG("volumetricvideo_start_decoding - id: %d", ID);

	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_start_decoding - cant find instance id: %d", ID);
		return -1;
	}

	// Start decoding
	return (*iter)->start();
}

// --------------------------------------------------------------------------
// Stop decoder 
//
VOLUMETRIC_VIDEO_API int	volumetricvideo_stop_decoding(int ID)
{
	LOG("volumetricvideo_stop_decoding - id: %d", ID);

	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_stop_decoding - cant find instance id: %d", ID);
		return -1;
	}

	// Stop decoding
	return (*iter)->stop();

}


// --------------------------------------------------------------------------
// Set Frame to display
//
VOLUMETRIC_VIDEO_API int	volumetricvideo_seek(int ID, double time)
{
	LOG("volumetricvideo_seek - id: %d - time: %f", ID, time);

	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_seek - cant find instance id: %d", ID);
		return -1;
	}

	//
	return (*iter)->seek(time);
}



// --------------------------------------------------------------------------
// Video functions
// --------------------------------------------------------------------------


// --------------------------------------------------------------------------
// Load Video Resources
//
VOLUMETRIC_VIDEO_API int	volumetricvideo_load_video(int ID, const char* filepath)
{
	LOG("volumetricvideo_load_video - id: %d", ID);

	// Get Instance
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_load_video - cant find instance id: %d", ID);
		return -1;
	}

	// Set Video
	if (!(*iter)->get_avdecoder_ptr()->init(filepath))
	{
		// Error loading video
		return -1;
	}
	if (!(*iter)->get_avdecoder_ptr()->has_embedded_geometry())
	{
		LOG("volumetricvideo_load_video - MP4 has no vvge geometry stream");
		return -1;
	}
	if (!(*iter)->get_geometrydecoder_ptr()->init())
	{
		LOG("volumetricvideo_load_video - failed to initialise embedded geometry");
		return -1;
	}

	//
	return 1;
}

VOLUMETRIC_VIDEO_API const char* volumetricvideo_get_last_error(int ID)
{
	static thread_local std::string error;
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		error = "Volumetric video instance was not found.";
		return error.c_str();
	}
	error = (*iter)->get_avdecoder_ptr()->get_last_error();
	if (error.empty())
		error = (*iter)->get_geometrydecoder_ptr()->get_last_error();
	return error.c_str();
}

VOLUMETRIC_VIDEO_API double volumetricvideo_get_last_presented_time(int ID)
{
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
		return -1.0;
	return (*iter)->get_last_presented_time();
}


// --------------------------------------------------------------------------
// Load Video Resources
//
VOLUMETRIC_VIDEO_API int	volumetricvideo_get_video_details(int ID, int& width, int& height, double& fps, double& duration)
{
	LOG("volumetricvideo_get_video_details - id: %d", ID);

	// Get Instance
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_get_video_details - cant find instance id: %d", ID);
		return -1;
	}

	// Get Video properties
	width		= (*iter)->get_avdecoder_ptr()->get_video_info().width;
	height		= (*iter)->get_avdecoder_ptr()->get_video_info().height;
	fps			= (*iter)->get_avdecoder_ptr()->get_video_info().fps;
	duration	= (*iter)->get_avdecoder_ptr()->get_video_info().total_time;

	return 1;
}

VOLUMETRIC_VIDEO_API int volumetricvideo_get_audio_details(
	int ID, int& sample_rate, int& channels)
{
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
		return -1;

	const IAVDecoder::AudioInfo info =
		(*iter)->get_avdecoder_ptr()->get_audio_info();
	if (!info.is_enabled)
	{
		sample_rate = 0;
		channels = 0;
		return 0;
	}

	sample_rate = info.sample_rate;
	channels = info.channels;
	return 1;
}

VOLUMETRIC_VIDEO_API int volumetricvideo_read_audio(
	int ID, float* samples, int sample_count)
{
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter) || samples == NULL || sample_count <= 0)
		return -1;

	return (*iter)->get_avdecoder_ptr()->read_audio(samples, sample_count);
}


// --------------------------------------------------------------------------
// Set unity DX11 Textures
//
VOLUMETRIC_VIDEO_API int	volumetricvideo_get_texture_pointers(int ID, void*& yPointer, void*& uPointer, void*& vPointer)
{
	LOG("volumetricvideo_set_texture_pointer - id: %d", ID);

	// Get Instance
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_get_texture_pointers - cant find instance id: %d", ID);
		return -1;
	}

	// Get width and height - this info is within the class, just a sanity check
	int width = (*iter)->get_avdecoder_ptr()->get_video_info().width;
	int height = (*iter)->get_avdecoder_ptr()->get_video_info().height;
	LOG("volumetricvideo_set_texture_pointer - %d x %d", width, height);

	// Create texture for instance 
	int ret = (*iter)->get_texture_ptr()->init(g_GraphicsDevice, width, height);
	if (ret == -1)
	{
		//
		return -1;
	}

	// now get pointers
	(*iter)->get_texture_ptr()->getResourcePointers(yPointer, uPointer, vPointer);

	//
	LOG("volumetricvideo_set_texture_pointer - end");
	return 1;	
}

VOLUMETRIC_VIDEO_API int volumetricvideo_register_texture_pointers(
	int ID, void* yPointer, void* uPointer, void* vPointer)
{
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter) ||
		yPointer == NULL || uPointer == NULL || vPointer == NULL)
		return -1;

	(*iter)->get_texture_ptr()->registerResourcePointers(
		yPointer, uPointer, vPointer);
	return 1;
}




//-----------------------------------------------
// Geometry Functions
//-----------------------------------------------


// --------------------------------------------------------------------------
// Set native mesh pointers
//
VOLUMETRIC_VIDEO_API int	volumetricvideo_set_mesh_pointer(int ID, void* index_buffer_handle, int index_count, void* vertex_buffer_handle, int vertex_count)
{
	LOG("volumetricvideo_set_mesh_pointer - id: %d", ID);

	// Get Instance
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		LOG("volumetricvideo_get_texture_pointers - cant find instance id: %d", ID);
		return -1;
	}
	
	//
	if (!(*iter)->get_meshbuffer()->init(g_GraphicsDevice, index_buffer_handle, index_count, vertex_buffer_handle, vertex_count))
	{
		return -1;
	}


	//
	return 1;
}
 
