# Compatibility Matrix

This document distinguishes configurations that have been manually validated
from build paths that are implemented but still require release validation.
Dependency versions are pinned by `OpenVolumetricNative/vcpkg.json`; the
values below describe the current 0.1 development line.

## Toolchain and dependencies

| Component | Supported baseline | Notes |
| --- | --- | --- |
| C++ | C++17 | Required by the core and installed SDK. |
| CMake | 3.20 or newer | Install/export package requires standard CMake package helpers. |
| vcpkg | Repository pinned baseline | Use manifest mode and the repository overlay triplets. |
| FFmpeg | 8.1.2 or compatible 8.x | `avformat`, `avcodec`, and `swresample`; authoring executable also needs x264/x265 and AAC. |
| Draco | 1.5.7 | Runtime decoding and in-process authoring encoding. |
| libcurl | 8.21 or pinned-compatible | Built with SSL support for HTTP range transport. |
| nlohmann-json | 3.12 or pinned-compatible | Adaptive manifest parsing. |

Static consumers must use dependencies compatible with the compiler,
architecture, runtime library, and deployment target used for
OpenVolumetric. Mixing archives from unrelated vcpkg triplets is unsupported.

## Engine and platform status

| Integration | Platform | Status |
| --- | --- | --- |
| Unity 6.0/6000.3 | macOS ARM64, Metal | Validated local, HTTP, fragmented, adaptive, authoring, seek, loop, and synchronized audio. |
| Unity 6.0/6000.3 | Windows x64, D3D11/D3D12 | Playback and both graphics backends validated; final Phase 6 performance comparison remains. |
| Unity 6.0/6000.3 | Android ARM64 API 29+, Vulkan | Native build and Quest 2/3S playback validated; release APK and sustained Quest 3S matrix remain. |
| Unreal Engine 5.8 | macOS ARM64 14+, Metal/RHI | Editor runtime, authoring, HTTP/adaptive playback, and staged SDK linkage validated. |
| Unreal Engine 5.8 | Windows x64 | Staged SDK rules are implemented; clean compilation and packaged playback remain unvalidated. |
| Native core | Linux x64 | Supported through the development container; clean CI/container validation remains outstanding. |

Unreal Android/Quest and other Unreal RHIs are not current release targets.
Unity and Unreal packaged-build support is distinct from Editor validation and
must be recorded separately before a public binary release.
