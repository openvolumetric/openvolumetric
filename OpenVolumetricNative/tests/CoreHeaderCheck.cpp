// This translation unit intentionally includes only the engine-neutral public
// contracts. Its CMake target receives core include paths and standard-library
// dependencies, but no Unity, Unreal, FFmpeg, Draco, or transport implementation
// include paths. A boundary leak therefore fails every normal native build.
#include <AdaptiveManifest.h>
#include <AdaptivePolicy.h>
#include <AdaptiveSelection.h>
#include <IAVDecoder.h>
#include <IByteSource.h>
#include <IGeometryDecoder.h>
#include <IVolumetricContainer.h>
#include <OpenVolumetricPlayer.h>

static_assert(sizeof(openvolumetric::OpenVolumetricMediaInfo) > 0,
	"The engine-neutral player façade must remain self-contained.");
