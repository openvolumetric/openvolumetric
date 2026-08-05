# Building OpenVolumetric

OpenVolumetric's native libraries use CMake 3.20 or newer, Ninja, and vcpkg
manifest mode. vcpkg downloads and builds FFmpeg, Draco, libcurl, OpenSSL,
zlib, and the header-only nlohmann JSON library. Runtime libraries are linked
into the produced engine plug-ins, so players do not require separately
installed native runtimes.

## Build outputs

| Target | Purpose |
| --- | --- |
| `OpenVolumetricCore` | Engine-independent MP4, media, geometry, timing, and playback core |
| `OpenVolumetricAuthoringCore` | Draco encoding, MP4 packaging, and verification |
| `OpenVolumetricAuthoring` | Unity Editor authoring shared library |
| `OpenVolumetricUnityPlugin` | CMake target for the Unity runtime and native DSP plug-in |
| `OpenVolumetricRuntime` | Unreal runtime module |
| `OpenVolumetricAuthoring` | Unreal Editor module |

## Development container

The preferred Linux core-development environment captures CMake, Ninja,
Clang, pkg-config, and vcpkg in Docker. The host requires:

- a Docker-compatible runtime;
- Visual Studio Code; and
- the **Dev Containers** extension.

To build:

1. Open the repository in Visual Studio Code.
2. Run **Dev Containers: Reopen in Container**.
3. Wait for the image and vcpkg checkout to finish.
4. Run **CMake: Configure** and select the `vcpkg` preset.
5. Run **CMake: Build**.

Equivalent container commands are:

```sh
cd OpenVolumetricNative
cmake --preset vcpkg
cmake --build --preset vcpkg
```

The container builds the portable Linux core. Platform graphics integrations
must be built with their native SDKs.

## Native host build

Host prerequisites:

- CMake 3.20 or newer;
- Ninja; and
- vcpkg, with `VCPKG_ROOT` set to its checkout directory.

Configure and build:

```sh
cd OpenVolumetricNative
cmake --preset vcpkg
cmake --build --preset vcpkg
```

The first dependency build can take several minutes, particularly FFmpeg and
OpenSSL. Subsequent builds reuse the vcpkg installed tree. CI should
additionally configure a vcpkg binary cache.

## Native tests

Desktop configurations build `OpenVolumetricCoreTests` by default through
CMake's standard `BUILD_TESTING` option. The fast suite creates its inputs in
memory or temporary files and currently covers geometry-packet framing and
dependencies, adaptive manifest validation and local representation selection,
fragmented-MP4 random-access index parsing, local byte-source seeking and
cancellation, failed-player lifecycle rollback, and topology identity across
vertex, UV, and winding changes. A reproducible 12 KiB three-track fixture also
covers successful open/start, complete presentation, active seek,
end-of-stream restart, repeated start/stop, and idempotent close.
Additional small fixtures cover independent geometry, no-audio playback,
fragmented MP4, adaptive package resolution, and recovery from a corrupt
temporal update at the next independent geometry sample.

When a Python 3 interpreter is available at configure time, CTest also builds
and runs `OpenVolumetricHttpTransportTests`. Its loopback-only server validates
bounded range reads and seeks, transient 503 recovery, permanent retry
exhaustion, truncated 206 responses, and cancellation of a delayed request.
The test chooses an ephemeral localhost port and requires no internet access.

Fixture contents and regeneration instructions are recorded in
[`tests/fixtures/README.md`](../OpenVolumetricNative/tests/fixtures/README.md).

After configuring and building the normal host preset, run:

```sh
cd OpenVolumetricNative
ctest --preset vcpkg
```

Use `-DBUILD_TESTING=OFF` when a production-only desktop build should omit the
test executable. Android configurations always omit the host test target and
its doctest dependency.

The complete automated and manual acceptance procedure is in
[TESTING.md](TESTING.md). Clang and GCC hosts can run ASan and UBSan through
the `vcpkg-sanitizers` configure, build, and test presets.

## Installable C++ SDK

The desktop build installs `OpenVolumetric::Core` and
`OpenVolumetric::Authoring` CMake package targets with a deliberate public
header set. A clean-consumer CTest verifies both installed targets without
including repository source directories. Installation, consumption, and
Unreal staging are documented in [SDK.md](SDK.md). Supported and pending
configurations are recorded in [COMPATIBILITY.md](COMPATIBILITY.md).

### macOS

The macOS build produces the Metal Unity runtime and the authoring library:

```text
Unity/Assets/Plugins/OpenVolumetric/macOS/
├── AudioPluginOpenVolumetricUnity.dylib
├── OpenVolumetricAuthoring.dylib
```

Use Metal as Unity's graphics API. The normal preset builds the host
architecture. The `AudioPlugin` filename prefix allows Unity to discover the
native audio effect exported by the same library as the runtime C API.

### Windows

The Windows build produces one Unity plug-in with D3D11 and D3D12 backends,
plus the authoring DLL. Build with MSVC on Windows; these implementations
cannot be produced by the Linux development container.

D3D11 and D3D12 local playback and seeking are manually validated. D3D12 also
builds cleanly with warnings treated as errors. Launch Unity with
`-force-d3d12` to select it without changing the project's saved graphics API
order.

## Quest/Android ARM64

Install Android Build Support through Unity Hub and use the SDK, OpenJDK,
CMake, and NDK shipped with the selected Unity Editor.

Set the NDK variables:

```sh
export ANDROID_NDK_ROOT=/path/to/Unity/PlaybackEngines/AndroidPlayer/NDK
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"
```

Configure and build:

```sh
cd OpenVolumetricNative
cmake --preset vcpkg-android-arm64
cmake --build --preset vcpkg-android-arm64
```

The stripped Vulkan plug-in is staged at:

```text
Unity/Assets/Plugins/OpenVolumetric/Android/arm64-v8a/
libAudioPluginOpenVolumetricUnity.so
```

The preset targets Android API 29 and `arm64-v8a`. See
[QUEST_BASELINE.md](QUEST_BASELINE.md) for the validated Unity and Quest
configuration. The Unity project forces the Android internet permission for
native HTTP(S) playback, and the Android libcurl/OpenSSL build uses the
platform trusted-certificate directory.

## Unreal Engine 5.8 on macOS

Configure an ARM64 macOS 14 SDK with the repository's vcpkg toolchain and
`arm64-osx-openvolumetric` overlay triplet, then install and stage it before
compiling the Unreal project.

```sh
cmake -S OpenVolumetricNative \
  -B OpenVolumetricNative/build/unreal-host-macos14 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-openvolumetric \
  -DVCPKG_OVERLAY_TRIPLETS=OpenVolumetricNative/triplets \
  -DBUILD_TESTING=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DCMAKE_INSTALL_PREFIX=/tmp/openvolumetric-unreal-sdk

cmake --build OpenVolumetricNative/build/unreal-host-macos14
cmake --install OpenVolumetricNative/build/unreal-host-macos14
python3 tools/stage_unreal_sdk.py \
  --sdk /tmp/openvolumetric-unreal-sdk \
  --dependencies OpenVolumetricNative/build/unreal-host-macos14/vcpkg_installed/arm64-osx-openvolumetric \
  --platform Mac
```

Build the Editor target with UnrealBuildTool:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealEditor Mac Development \
  /path/to/openvolumetric/Unreal/Unreal.uproject \
  -WaitMutex
```

Alternatively, open `Unreal/Unreal.uproject` in Unreal Engine 5.8 and allow
the Editor to compile the modules.

The Unreal modules consume only this staged SDK, not the repository source or
CMake build tree. The same build rules accept a staged Win64 SDK. The macOS
Editor build is validated; Win64 and packaged-build validation remain
outstanding.

## Authoring dependency

Runtime decoding needs no external FFmpeg installation. Authoring in either
engine currently invokes an external FFmpeg executable for image-sequence,
video, and audio encoding.

The selected executable must include:

- `libx264` for H.264 presets;
- `libx265` for HEVC presets; and
- AAC encoding support.

Draco encoding and MP4 packaging are linked into the OpenVolumetric authoring
libraries and do not require standalone command-line tools.

## Generated files

Native build trees, vcpkg installations, Unity generated directories, Unreal
`Binaries`, `Intermediate`, `Saved`, and locally built shared libraries are
ignored. Keep Unity `.meta` files tracked because they preserve plug-in
platform and architecture settings.
