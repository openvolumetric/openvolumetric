# Native SDK Packaging

OpenVolumetric installs an engine-neutral C++17 SDK rather than requiring a
consumer to include files from the source tree. The installed package exports:

- `OpenVolumetric::Core` for playback, adaptive selection, and diagnostics;
- `OpenVolumetric::Authoring` for presets, validation, Draco encoding, and MP4
  packaging on desktop hosts.

Concrete FFmpeg, Draco, HTTP, queue, and fragmented-index implementation
headers are intentionally private. The installed headers contain only the
engine-neutral façades and contracts required by those façades.

## Install and consume

Choose an installation prefix while configuring, then build and install:

```sh
cmake -S OpenVolumetricNative \
  -B OpenVolumetricNative/build/sdk \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_INSTALL_PREFIX=/path/to/openvolumetric-sdk
cmake --build OpenVolumetricNative/build/sdk
cmake --install OpenVolumetricNative/build/sdk
```

A CMake consumer uses the package without repository-relative include paths:

```cmake
find_package(OpenVolumetric CONFIG REQUIRED)
target_link_libraries(MyPlayer PRIVATE OpenVolumetric::Core)
```

The package locates FFmpeg, curl, Draco, and nlohmann-json through the
consumer's toolchain. A static SDK consumer must therefore use a compatible
dependency prefix, normally the same pinned vcpkg manifest and triplet used to
build OpenVolumetric. `OpenVolumetricInstalledConsumerTest` installs into a
fresh directory and compiles independent runtime and authoring executables on
every desktop native test run.

## Unreal staging

UnrealBuildTool does not consume CMake package exports. Generate a staged
Unreal SDK after installing the native SDK:

```sh
python3 tools/stage_unreal_sdk.py \
  --sdk /path/to/openvolumetric-sdk \
  --dependencies OpenVolumetricNative/build/sdk/vcpkg_installed/TRIPLET \
  --platform Mac
```

For Win64, build the SDK and dependencies with vcpkg's
`x64-windows-static-md` triplet. It produces static archives while retaining
the dynamic MSVC runtime used by Unreal. Run the commands from one Visual
Studio x64 Developer Prompt so CMake, vcpkg, and Unreal use a compatible MSVC
toolset. Then stage with `--platform Win64`:

```powershell
cmake -S OpenVolumetricNative `
  -B OpenVolumetricNative/build/unreal-host-win64 `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
  -DBUILD_TESTING=OFF `
  -DCMAKE_INSTALL_PREFIX=OpenVolumetricNative/build/sdk-win64
cmake --build OpenVolumetricNative/build/unreal-host-win64
cmake --install OpenVolumetricNative/build/unreal-host-win64
python tools/stage_unreal_sdk.py `
  --sdk OpenVolumetricNative/build/sdk-win64 `
  --dependencies OpenVolumetricNative/build/unreal-host-win64/vcpkg_installed/x64-windows-static-md `
  --platform Win64
```

The generated layout is:

```text
Unreal/Plugins/OpenVolumetric/Source/ThirdParty/OpenVolumetricSDK/
├── include/OpenVolumetric/
└── lib/
    ├── Mac/
    └── Win64/
```

This directory is ignored by Git because it contains generated headers and
platform archives. Include it in a packaged plug-in release, or set
`OPENVOLUMETRIC_UNREAL_SDK` to an equivalent external directory. The runtime
and authoring modules share one `OpenVolumetricSDK` external-module rule, so
neither module knows the repository location, native build directory, or
vcpkg layout.

The staged libraries must use the same architecture, C++ runtime, deployment
target, and build configuration as Unreal. On macOS, build both OpenVolumetric
and its vcpkg dependencies with the repository's
`arm64-osx-openvolumetric` triplet and a macOS 14 deployment target.
On Windows, do not use `x64-windows-static`: its static MSVC runtime is not
compatible with Unreal's dynamic runtime.

Win64 staging and independent runtime and authoring consumer linkage are
validated with `x64-windows-static-md`. Unreal Editor module compilation still
requires an installed compatible Unreal Engine toolchain.
