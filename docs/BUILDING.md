# Building OpenVolumetric

OpenVolumetric's native libraries use CMake 3.20 or newer, Ninja, and vcpkg
manifest mode. vcpkg downloads and builds FFmpeg, Draco, libcurl, OpenSSL,
and zlib. Runtime libraries are linked into the produced engine plug-ins, so
players do not require separately installed native runtimes.

## Build outputs

| Target | Purpose |
| --- | --- |
| `OpenVolumetricCore` | Engine-independent MP4, media, geometry, timing, and playback core |
| `OpenVolumetricAuthoringCore` | Draco encoding, MP4 packaging, and verification |
| `OpenVolumetricAuthoring` | Unity Editor authoring shared library |
| `OpenVolumetricUnityPlugin` | Unity runtime native plug-in |
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

### macOS

The macOS build produces the Metal Unity runtime and the authoring library:

```text
Unity/Assets/Plugins/OpenVolumetric/macOS/
├── OpenVolumetricAuthoring.dylib
└── OpenVolumetricUnityPlugin.dylib
```

Use Metal as Unity's graphics API. The normal preset builds the host
architecture.

### Windows

The Windows build produces the D3D11 Unity plug-in and authoring DLL. Build
with MSVC on Windows; the D3D11 implementation cannot be produced by the
Linux development container.

The implementation exists, but a clean build and runtime test on the current
repository remain outstanding.

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
libOpenVolumetricUnityPlugin.so
```

The preset targets Android API 29 and `arm64-v8a`. See
[QUEST_BASELINE.md](QUEST_BASELINE.md) for the validated Unity and Quest
configuration. The Unity project forces the Android internet permission for
native HTTP(S) playback, and the Android libcurl/OpenSSL build uses the
platform trusted-certificate directory.

## Unreal Engine 5.8 on macOS

The current Unreal modules link an ARM64 macOS 14 native build from:

```text
OpenVolumetricNative/build/unreal-host-macos14
```

Configure that native tree with the repository's vcpkg toolchain and
`arm64-osx-openvolumetric` overlay triplet, then build it before compiling the
Unreal project.

```sh
cmake -S OpenVolumetricNative \
  -B OpenVolumetricNative/build/unreal-host-macos14 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-openvolumetric \
  -DVCPKG_OVERLAY_TRIPLETS=OpenVolumetricNative/triplets \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0

cmake --build OpenVolumetricNative/build/unreal-host-macos14
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

The current Unreal build has been validated in the macOS Editor. Packaged
dependency staging and packaged-build validation remain outstanding.

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
