#include <AdaptiveManifest.h>
#include <AdaptivePlayerCoordinator.h>
#include <OpenVolumetricPlayer.h>

int main()
{
	openvolumetric::OpenVolumetricPlayer player;
	openvolumetric::AdaptiveManifest manifest;
	return player.media_info().width + static_cast<int>(manifest.version);
}
