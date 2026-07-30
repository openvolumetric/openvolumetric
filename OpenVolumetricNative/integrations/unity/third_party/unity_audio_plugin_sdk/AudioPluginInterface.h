// Minimal ABI declarations adapted from Unity Technologies'
// NativeAudioPlugins AudioPluginInterface.h (API version 0x010402).
// The upstream SDK is MIT licensed; see LICENSE in this directory.
#pragma once

#include <cassert>
#include <cstdint>

#define UNITY_AUDIO_PLUGIN_API_VERSION 0x010402

#if defined(_WIN32)
#define UNITY_AUDIODSP_CALLBACK __stdcall
#define UNITY_AUDIODSP_EXPORT_API __declspec(dllexport)
#define AUDIO_CALLING_CONVENTION
#else
#define UNITY_AUDIODSP_CALLBACK
#define UNITY_AUDIODSP_EXPORT_API __attribute__((visibility("default")))
#define AUDIO_CALLING_CONVENTION
#endif

#define UNITY_AUDIODSP_RESULT int

using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;

enum
{
	UNITY_AUDIODSP_OK = 0,
	UNITY_AUDIODSP_ERR_UNSUPPORTED = 1,
};

enum UnityAudioEffectDefinitionFlags
{
	UnityAudioEffectDefinitionFlags_IsSideChainTarget = 1 << 0,
	UnityAudioEffectDefinitionFlags_IsSpatializer = 1 << 1,
	UnityAudioEffectDefinitionFlags_IsAmbisonicDecoder = 1 << 2,
	UnityAudioEffectDefinitionFlags_AppliesDistanceAttenuation = 1 << 3,
	UnityAudioEffectDefinitionFlags_NeedsSpatializerData = 1 << 4,
};

enum UnityAudioEffectStateFlags
{
	UnityAudioEffectStateFlags_IsPlaying = 1 << 0,
	UnityAudioEffectStateFlags_IsPaused = 1 << 1,
	UnityAudioEffectStateFlags_IsMuted = 1 << 2,
	UnityAudioEffectStateFlags_IsSideChainTarget = 1 << 3,
};

struct UnityAudioEffectState;
struct UnityAudioSpatializerData;
struct UnityAudioAmbisonicData;

using UnityAudioEffect_CreateCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*);
using UnityAudioEffect_ReleaseCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*);
using UnityAudioEffect_ResetCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*);
using UnityAudioEffect_ProcessCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*, float*, float*, unsigned int, int, int);
using UnityAudioEffect_SetPositionCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*, unsigned int);
using UnityAudioEffect_SetFloatParameterCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*, int, float);
using UnityAudioEffect_GetFloatParameterCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*, int, float*, char*);
using UnityAudioEffect_GetFloatBufferCallback =
	UNITY_AUDIODSP_RESULT (UNITY_AUDIODSP_CALLBACK*)(
		UnityAudioEffectState*, const char*, float*, int);

struct UnityAudioEffectState
{
	union
	{
		struct
		{
			UInt32 structsize;
			UInt32 samplerate;
			UInt64 currdsptick;
			UInt64 prevdsptick;
			float* sidechainbuffer;
			void* effectdata;
			UInt32 flags;
			void* internal;
			UnityAudioSpatializerData* spatializerdata;
			UInt32 dspbuffersize;
			UInt32 hostapiversion;
			UnityAudioAmbisonicData* ambisonicdata;
		};
		unsigned char pad[80];
	};

	template<typename T>
	T* GetEffectData() const
	{
		assert(effectdata);
		return static_cast<T*>(effectdata);
	}
};

struct UnityAudioParameterDefinition
{
	char name[16];
	char unit[16];
	const char* description;
	float min;
	float max;
	float defaultval;
	float displayscale;
	float displayexponent;
};

struct UnityAudioEffectDefinition
{
	UInt32 structsize;
	UInt32 paramstructsize;
	UInt32 apiversion;
	UInt32 pluginversion;
	UInt32 channels;
	UInt32 numparameters;
	UInt64 flags;
	char name[32];
	UnityAudioEffect_CreateCallback create;
	UnityAudioEffect_ReleaseCallback release;
	UnityAudioEffect_ResetCallback reset;
	UnityAudioEffect_ProcessCallback process;
	UnityAudioEffect_SetPositionCallback setposition;
	UnityAudioParameterDefinition* paramdefs;
	UnityAudioEffect_SetFloatParameterCallback setfloatparameter;
	UnityAudioEffect_GetFloatParameterCallback getfloatparameter;
	UnityAudioEffect_GetFloatBufferCallback getfloatbuffer;
};

extern "C" UNITY_AUDIODSP_EXPORT_API int AUDIO_CALLING_CONVENTION
UnityGetAudioEffectDefinitions(UnityAudioEffectDefinition*** definitions);
