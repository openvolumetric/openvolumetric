# unity-volumetric-video

This project enables the playback of volumetric video in unity game engine.
This is implemented as a native C++ plugin


### Encoding Content

#### Geometry
Geometry is encoded using google draco. The following command can be used to encode a single mesh 

``
    draco_encoder -i input.obj -o encoded.drc 
``

#### Texture
Textures are enocded using ffmpeg h265. The following command can be used to encode Textures

``
    ffmpeg -i %06d.png  -s 1024x1024 -c:v libx265 -crf 25  -pix_fmt yuv420p -x265-params keyint=20:min-keyint=1:bframes=0:slices=6 -r $FRAMERATE output.mmp4
``

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
NativePlugin/
├── src/core/
│   ├── decoding/
│   ├── geometry/
│   ├── media/
│   └── support/
└── integrations/
    └── unity/
        ├── include/Unity/
        └── src/
            ├── api/
            └── rendering/
```

`VolumetricVideoCore` contains the portable decoding and data model. The
`VolumetricVideoNativePlugin` target is the Unity integration and links the
core to Unity's D3D11 or Metal rendering API. Future engine integrations, such
as Unreal, can live beside `integrations/unity` and link the same core target.

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
cd NativePlugin
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
cd NativePlugin
cmake --preset vcpkg
cmake --build --preset vcpkg
```

On Windows this builds and stages `VolumetricVideoNativePlugin.dll` in the
Unity plugin directory. On macOS it builds the Metal implementation and stages
`VolumetricVideoNativePlugin.dylib` in
`Unity/Assets/Plugins/VolumetricVideo/macOS`. Linux builds the portable decoder
core only.

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
