#include "unity-volumetric-video-api.h"

// DirectX11
#include <d3d11.h>

// Unity Interface
#include <Unity/IUnityInterface.h>
#include <Unity/IUnityGraphics.h>
#include <Unity/IUnityGraphicsD3D11.h>

// STL + C
#include <assert.h>
#include <list>
#include <iostream>

//
#include <VolumetricVideoD3D11.h>
#include <IVolumetricVideo.h>
#include <FFMPEGTools.h>
#include <Logger.h>

// --------------------------------------------------------------------------
// Global variables for plugin
std::list<IVolumetricVideo*> vv_instances;
typedef std::list<IVolumetricVideo*>::iterator VolumetricVideo_iter;



// --------------------------------------------------------------------------
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
// SetTimeFromUnity, an example function we export which is called by one of the scripts.
static float g_Time;
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity(float t) { g_Time = t; }



// --------------------------------------------------------------------------
// UnitySetInterfaces

//
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

//
static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

//
extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces * unityInterfaces)
{
	s_UnityInterfaces	= unityInterfaces;
	s_Graphics			= s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

//
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

// --------------------------------------------------------------------------
// GraphicsDeviceEvent
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

//
static ID3D11Device* g_D3D11Device = NULL;

//
static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsD3D11* d3d11 = s_UnityInterfaces->Get<IUnityGraphicsD3D11>();
		g_D3D11Device = d3d11->GetDevice();
	}
}

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

	// TODO adapt to platform
	//#if SUPPORT_D3D11
	if (currentDeviceType == kUnityGfxRendererD3D11)
	{
		DoEventGraphicsDeviceD3D11(eventType);
	}
	//#endif
}



// --------------------------------------------------------------------------
// OnRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.
static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	// Unknown / unsupported graphics device type? Do nothing
	if (s_DeviceType == kUnityGfxRendererNull)
		return;

	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(eventID, &iter))
	{
		(*iter)->render();
	}
}



// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}

// --------------------------------------------------------------------------
// Open external console to see c++ debug  info
__declspec(dllexport) void	volumetricvideo_open_external_console()
{
	Logger::instance()->open_external_console();
}

// --------------------------------------------------------------------------
// Close external console to see c++ debug  info
__declspec(dllexport) void	volumetricvideo_close_external_console()
{
	Logger::instance()->close_external_console();
}


// --------------------------------------------------------------------------
// Init
__declspec(dllexport) int volumetricvideo_init(int& ID)
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

	// If this is the first instance then increment
	if (ID == 0)
	{
		ffmpegtools::register_ffmpeg();
	}

	//Create Volumetric Video Decoder
	IVolumetricVideo* vv = new VolumetricVideoD3D11(ID);

	// Add to instance list
	vv_instances.push_back(vv);

	// Complete
	LOG("volumetricvideo_init - end");

	// Done :)	
	return 1;
}

// --------------------------------------------------------------------------
// Quit application
__declspec(dllexport) void	volumetricvideo_quit(int ID)
{
	LOG("volumetricvideo_quit - id: %d",ID);

	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		return;
	}

	// delete contents of pointer and erase from list
	delete (*iter);
	vv_instances.erase(iter);
}


// --------------------------------------------------------------------------
// Frame Index to display
__declspec(dllexport) int	SetFrame(int ID, int frame_index)
{
	//
	VolumetricVideo_iter iter;
	if (!get_vv_instance(ID, &iter))
	{
		return (*iter)->set_frame(frame_index);
	}

	//
	return 0;
}



// --------------------------------------------------------------------------
// Load video function
__declspec(dllexport) int	LoadVideo(int ID, const char* filename, int& fps, int& width, int& height)
{


	return 0;
}



// --------------------------------------------------------------------------
// Set texture Pointer
__declspec(dllexport) int	SetTexturePointer(int ID, void*& yPointer, void*& uPointer, void*& vPointer)
{


	return 0;
}
