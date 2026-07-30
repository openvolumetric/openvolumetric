#include "UnityAudioBridge.h"

#include <AudioPluginInterface.h>

#include <cstring>

namespace
{
UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK create_effect(
	UnityAudioEffectState* state)
{
	state->effectdata = nullptr;
	return UNITY_AUDIODSP_OK;
}

UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK release_effect(
	UnityAudioEffectState* state)
{
	state->effectdata = nullptr;
	return UNITY_AUDIODSP_OK;
}

UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK reset_effect(
	UnityAudioEffectState*)
{
	return UNITY_AUDIODSP_OK;
}

UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK process_effect(
	UnityAudioEffectState* state,
	float*,
	float* output,
	unsigned int length,
	int,
	int output_channels)
{
	openvolumetric::unity::process_dsp_audio(
		output,
		length,
		output_channels,
		state->currdsptick,
		state->samplerate);
	return UNITY_AUDIODSP_OK;
}

UnityAudioEffectDefinition g_definition{};
UnityAudioEffectDefinition* g_definitions[] = {&g_definition};
}

extern "C" UNITY_AUDIODSP_EXPORT_API int AUDIO_CALLING_CONVENTION
UnityGetAudioEffectDefinitions(UnityAudioEffectDefinition*** definitions)
{
	static bool initialized = false;
	if (!initialized)
	{
		g_definition.structsize = sizeof(UnityAudioEffectDefinition);
		g_definition.paramstructsize =
			sizeof(UnityAudioParameterDefinition);
		g_definition.apiversion = UNITY_AUDIO_PLUGIN_API_VERSION;
		g_definition.pluginversion = 0x000100;
		g_definition.channels = 0;
		g_definition.numparameters = 0;
		g_definition.flags =
			UnityAudioEffectDefinitionFlags_IsSpatializer;
		std::strncpy(
			g_definition.name,
			"OpenVolumetric Audio",
			sizeof(g_definition.name) - 1);
		g_definition.create = create_effect;
		g_definition.release = release_effect;
		g_definition.reset = reset_effect;
		g_definition.process = process_effect;
		initialized = true;
	}
	*definitions = g_definitions;
	return 1;
}
