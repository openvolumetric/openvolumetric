#include "UnityAudioBridge.h"

#include <AudioPluginInterface.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
/// Applies inexpensive equal-power stereo panning from Unity's source and
/// inverse-listener transforms. Unity retains ownership of distance rolloff.
void spatialize_stereo(
	const UnityAudioSpatializerData* spatializer,
	float* output,
	unsigned int frame_count,
	int output_channels)
{
	if (spatializer == nullptr || output == nullptr || output_channels != 2)
		return;

	const float* source = spatializer->sourcematrix;
	const float* listener = spatializer->listenermatrix;
	const float source_x = source[12];
	const float source_y = source[13];
	const float source_z = source[14];
	const float relative_x = listener[0] * source_x +
		listener[4] * source_y + listener[8] * source_z + listener[12];
	const float relative_z = listener[2] * source_x +
		listener[6] * source_y + listener[10] * source_z + listener[14];
	const float horizontal_distance = std::sqrt(
		relative_x * relative_x + relative_z * relative_z);
	const float pan = horizontal_distance > 0.0001F
		? std::clamp(relative_x / horizontal_distance, -1.0F, 1.0F)
		: 0.0F;
	constexpr float half_pi = 1.57079632679F;
	const float angle = (pan + 1.0F) * 0.5F * half_pi;
	const float left_gain = std::cos(angle);
	const float right_gain = std::sin(angle);
	const float blend = std::clamp(spatializer->spatialblend, 0.0F, 1.0F);

	for (unsigned int frame = 0; frame < frame_count; ++frame)
	{
		float* sample = output + frame * 2;
		const float original_left = sample[0];
		const float original_right = sample[1];
		const float mono = 0.5F * (original_left + original_right);
		sample[0] = original_left * (1.0F - blend) +
			mono * left_gain * blend;
		sample[1] = original_right * (1.0F - blend) +
			mono * right_gain * blend;
	}
}

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
	spatialize_stereo(
		state->spatializerdata,
		output,
		length,
		output_channels);
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
		constexpr char effect_name[] = "OpenVolumetric Audio";
		static_assert(sizeof(effect_name) <= sizeof(g_definition.name));
		std::memcpy(g_definition.name, effect_name, sizeof(effect_name));
		g_definition.create = create_effect;
		g_definition.release = release_effect;
		g_definition.reset = reset_effect;
		g_definition.process = process_effect;
		initialized = true;
	}
	*definitions = g_definitions;
	return 1;
}
