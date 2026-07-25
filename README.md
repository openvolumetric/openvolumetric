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


### Contributors:

- Marco Volino - m.volino@surrey.ac.uk



### Limitations

- **Mesh Size**: Meshes can consist of up to 65,535 verticies and 100,000 triangles. This can be changed in the __VolumetricVideoDecoder.cs__ file.

### Building the native plugin

The native build uses CMake and vcpkg manifest mode. Draco remains pinned as a
Git submodule; FFmpeg is downloaded and built by vcpkg.

#### VS Code development container

The preferred Linux development environment captures CMake, Ninja, Clang,
pkg-config, and a pinned vcpkg checkout in Docker. The host only needs a
Docker-compatible runtime, VS Code, and the **Dev Containers** extension.

1. Open the repository in VS Code.
2. Run **Dev Containers: Reopen in Container** from the command palette.
3. Wait for the image and repository submodules to finish initializing.
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
artifacts still require a Windows/MSVC build, and future macOS plugin artifacts
will require Apple's native SDK.

#### Native host build

Use this only when producing or testing a native platform artifact.

Prerequisites:

- CMake 3.20 or newer
- Ninja
- vcpkg, with `VCPKG_ROOT` set to its checkout directory

Configure and build:

```sh
git submodule update --init --recursive
cd NativePlugin
cmake --preset vcpkg
cmake --build --preset vcpkg
```

On Windows this builds and stages `VolumetricVideoNativePlugin.dll` in the
Unity plugin directory. On macOS and Linux the current milestone builds the
portable decoder core only, because the Unity rendering bridge still depends
on D3D11.

The first FFmpeg build can take a significant amount of time. Subsequent builds
reuse vcpkg's installed tree; CI should additionally configure a vcpkg binary
cache.
