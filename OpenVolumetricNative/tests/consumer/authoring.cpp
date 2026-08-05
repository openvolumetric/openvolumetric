#include <AuthoringWorkflow.h>

int main()
{
	const auto settings = openvolumetric::authoring::preset_settings(
		openvolumetric::authoring::PlatformPreset::DesktopLocal);
	return settings.crf - 20;
}
