# Open Volumetric (OpenVol)

Open Volumetric is an open, cross-platform volumetric-video playback and
authoring project, known as OpenVol in code. It currently provides a native
C++ core and Unity integration, with Unreal integration planned.

The active development roadmap is tracked in
[docs/PLAN.md](docs/PLAN.md).


### Encoding Content

Runtime content is a single MP4 containing HEVC texture video, timed Draco
geometry samples, and optional audio. For one-click authoring from raw
numbered images and OBJ meshes, open
**Tools > OpenVol > Encoder** in Unity. The Editor tool performs
Draco conversion, media encoding, packaging, and verification. See
[`Unity/Assets/Editor/README.md`](Unity/Assets/Editor/README.md) for its input
conventions and tool discovery.

If the media file also contains an audio stream, the native plugin decodes it
to interleaved stereo floating-point PCM. Unity plays it through a streaming
`AudioClip` scheduled from the same DSP timestamp as the geometry and texture
streams. Media without audio continues to play normally.


### Contributors:

- Marco Volino - m.volino@surrey.ac.uk



### Limitations

- **Mesh Size**: Meshes can consist of up to 65,535 verticies and 100,000 triangles. This can be changed in the __VolumetricVideoDecoder.cs__ file.

### Building the native plugin

The native build uses CMake and vcpkg manifest mode. FFmpeg and Draco are
downloaded and built by vcpkg.

The native source is divided into an engine-independent core and engine
integrations:

```text
OpenVolNative/
├── src/
│   ├── core/
│   │   ├── decoding/
│   │   ├── geometry/
│   │   ├── media/
│   │   └── support/
│   └── authoring/
└── integrations/
    └── unity/
        ├── include/Unity/
        └── src/
            ├── api/
            └── rendering/
```

`OpenVolCore` contains the portable decoding and data model. The
`OpenVolUnityPlugin` target is the Unity integration and links the
core to Unity's D3D11 or Metal rendering API. Future engine integrations, such
as Unreal, can live beside `integrations/unity` and link the same core target.
See [`OpenVolNative/src/core/README.md`](OpenVolNative/src/core/README.md) for
the runtime data flow, directory responsibilities, threading, and ownership.

`OpenVolAuthoringCore` contains MP4 packaging and verification.
`OpenVolAuthoring` exposes it to the Unity Editor without adding
authoring code to player builds.

#### VS Code development container

The preferred Linux development environment captures CMake, Ninja, Clang,
pkg-config, and a pinned vcpkg checkout in Docker. The host only needs a
Docker-compatible runtime, VS Code, and the **Dev Containers** extension.

1. Open the repository in VS Code.
2. Run **Dev Containers: Reopen in Container** from the command palette.
3. Wait for the development container image to finish building.
4. Run **CMake: Configure**, select the `vcpkg` preset, then run
   **CMake: Build**.

The equivalent commands in the container terminal are:

```sh
cd OpenVolNative
cmake --preset vcpkg
cmake --build --preset vcpkg
```

The ignored, per-platform build tree preserves installed dependencies and a
vcpkg binary cache across container rebuilds. The first FFmpeg build can still
take several minutes.

This container builds the portable Linux decoder core. Windows D3D11 plugin
artifacts still require a Windows/MSVC build, and macOS Metal plugin artifacts
require macOS and Apple's native SDK.

#### Native host build

Use this only when producing or testing a native platform artifact.

Prerequisites:

- CMake 3.20 or newer
- Ninja
- vcpkg, with `VCPKG_ROOT` set to its checkout directory

Configure and build:

```sh
cd OpenVolNative
cmake --preset vcpkg
cmake --build --preset vcpkg
```

On Windows this builds and stages `OpenVolUnityPlugin.dll` in the
Unity plugin directory. On macOS it builds the Metal implementation and stages
`OpenVolUnityPlugin.dylib` in
`Unity/Assets/Plugins/OpenVol/macOS`. Linux builds the portable decoder
core only.

For Meta Quest, configure and build the Android ARM64 preset using Unity's
installed Android NDK:

```sh
export ANDROID_NDK_ROOT=/path/to/Unity/PlaybackEngines/AndroidPlayer/NDK
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"
cd OpenVolNative
cmake --preset vcpkg-android-arm64
cmake --build --preset vcpkg-android-arm64
```

The Android build stages the Vulkan plugin in
`Unity/Assets/Plugins/Android/arm64-v8a`. Quest builds optionally create a
camera-attached developer overlay when `Enable Developer Overlay` is selected
on the `VolumetricVideo` component:

- A / right primary: play or pause
- B / right secondary: toggle looping
- X / left primary: seek backward 10 seconds
- Y / left secondary: seek forward 10 seconds
- Left menu: show or hide the overlay

The normal macOS preset builds for the host architecture. This repository
currently targets Unity 2019.4, whose macOS editor requires an Intel plugin.
On an Apple Silicon Mac, build that version explicitly with:

```sh
cmake --preset vcpkg-macos-x64
cmake --build --preset vcpkg-macos-x64
```

Use Metal as the Unity player's graphics API. The plugin has no separately
installed FFmpeg runtime dependency; vcpkg links its FFmpeg libraries into the
native plugin.

The first FFmpeg build can take a significant amount of time. Subsequent builds
reuse vcpkg's installed tree; CI should additionally configure a vcpkg binary
cache.

### Future development

- Replace the macOS backend's per-frame Metal staging-buffer allocations with
  a reusable ring sized for the maximum number of frames in flight. Preserve
  the current command-buffer ordering so texture and mesh memory is never
  overwritten while the GPU is reading it. Consider representing this as a
  cross-platform upload abstraction: Metal and future Vulkan backends would
  manage an explicit ring and synchronization, while D3D11 can continue using
  its driver-managed `D3D11_MAP_WRITE_DISCARD` buffer renaming. Validate the
  change with frame-time and allocation profiling and confirm that texture
  flicker does not return.
