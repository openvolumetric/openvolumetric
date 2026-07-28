# Meta Quest 3S platform baseline

Milestone 8 initially targets the following reproducible toolchain:

- Unity 6.0 LTS, with this repository currently validated against
  `6000.3.17f1`.
- Unity Hub's Android SDK, OpenJDK 17, CMake 3.22.1, and Android NDK r27c.
- Android API 29 as the minimum player and native-library API.
- Android ARM64 (`arm64-v8a`) only.
- Vulkan as the initial and only graphics API. OpenGL ES is deferred.
- OpenXR Plugin 1.16.1, XR Plugin Management 4.5.3, and Unity OpenXR Meta
  2.4.0.
- Meta Quest 3S on a currently supported Quest OS release. Device testing
  must record the precise OS build because headset software updates
  independently of the application.

Unity 6 supports Android API 23 or newer, ARM64, and Vulkan. API 29 is selected
here as a deliberate Quest baseline rather than Unity's broad platform
minimum. Unity 6's supported Android development dependencies are installed
through Unity Hub; do not replace the NDK independently.

## Native Android core build

Set the tool locations supplied by the installed Unity editor:

```sh
export ANDROID_NDK_ROOT="/path/to/Unity/PlaybackEngines/AndroidPlayer/NDK"
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"
export VCPKG_ROOT="/path/to/vcpkg"

cd OpenVolumetricNative
cmake --preset vcpkg-android-arm64
cmake --build --preset vcpkg-android-arm64
```

The vcpkg Android ports use a host `pkg-config` executable while packaging
cross-compiled dependencies. The development container already supplies it.
For a native macOS build host, it can be bootstrapped through vcpkg rather
than installed system-wide; ensure that vcpkg's host `pkgconf` executable is
available as `pkg-config` on `PATH` during the first configure.

This preset builds the portable core, its FFmpeg/Draco dependencies, and the
Vulkan Unity plug-in. The stripped ARM64 library is staged at:

```text
Unity/Assets/Plugins/OpenVolumetric/Android/arm64-v8a/
libOpenVolumetricUnityPlugin.so
```

The Vulkan backend creates three `VK_FORMAT_R8_UNORM` images for the Y, U, and
V planes. Texture and Unity-owned mesh buffers are updated on Unity's current
Vulkan command buffer. Four host-visible staging slots are reused only after
Unity reports their frame as safe, avoiding writes into GPU work that is still
in flight.

## Unity configuration

The project is configured for Android ARM64 and Vulkan. After package
resolution, enable the OpenXR provider for the Android build target and select
the Meta Quest feature group in **Project Settings > XR Plug-in Management**.

Volumetric MP4 files remain authoring assets in `StreamingAssets`. Android
packages that directory inside the APK, so `StreamingAssetFile` copies the
selected file to `Application.persistentDataPath` before passing a readable
filesystem path to FFmpeg.

The first headset build must use Vulkan. The plug-in intentionally rejects
other graphics APIs until an explicitly tested fallback is added.

## Device validation record

For each physical Quest run, record:

- Quest OS build and headset model.
- Content resolution, codec profile, frame rate, duration, and bitrate.
- Average and worst CPU/GPU frame time.
- Decoder, geometry, and audio queue depths and dropped presentations.
- Memory use after startup and after repeated loops/seeks.
- Thermal state and battery drain during a sustained run.
- Whether FFmpeg software HEVC decoding meets the target. MediaCodec should
  only be introduced if device measurements require it.
