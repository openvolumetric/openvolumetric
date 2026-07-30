# Third-party notices

This document records the principal third-party software used by
OpenVolumetric. It is an attribution summary, not a replacement for the
complete license texts supplied by those projects.

## FFmpeg

OpenVolumetric uses FFmpeg libraries for MP4 demultiplexing and muxing, video
and audio decoding, audio resampling, and output verification.

- Project: <https://ffmpeg.org/>
- License: GNU Lesser General Public License, version 2.1 or later for the
  configuration used by OpenVolumetric. Optional FFmpeg components can change
  the applicable license.
- Source and legal guidance:
  <https://ffmpeg.org/download.html> and <https://ffmpeg.org/legal.html>

The runtime dependency is acquired by vcpkg with FFmpeg default features
disabled and only `avcodec`, `avformat`, and `swresample` requested. The
repository currently pins vcpkg baseline
`40f3c709db80acf154ac4b17a1f83c564ebd022e` and FFmpeg port version
`8.1.2#3` in `OpenVolumetricNative/vcpkg.json`.

OpenVolumetric runtime binaries may statically incorporate FFmpeg libraries.
Distributors must satisfy the LGPL requirements applicable to their exact
binary, including corresponding source, notices, modification/build
information, and the ability to replace or relink the LGPL-covered library.
See `docs/LICENSING.md`.

The external FFmpeg executable selected by an authoring user is not included
in this repository. Its applicable license depends on how that executable was
built. Builds containing components such as libx264 or libx265 may be covered
by the GPL.

## Draco

OpenVolumetric uses Draco for mesh encoding and decoding.

- Project: <https://github.com/google/draco>
- License: Apache License 2.0
- Copyright: Draco contributors

The dependency is acquired by vcpkg. The repository currently requests Draco
port version `1.5.7#1`.

## libcurl, OpenSSL, and zlib

OpenVolumetric uses libcurl for HTTP and HTTPS byte-range transport. The
vcpkg configuration enables libcurl's OpenSSL backend and also brings in
zlib.

- libcurl: <https://curl.se/> — curl license
- OpenSSL: <https://www.openssl.org/> — Apache License 2.0
- zlib: <https://zlib.net/> — zlib license

These dependencies are acquired by vcpkg and may be statically incorporated
into runtime binaries. Distributors must retain the applicable notices and
license texts for the exact libraries included in their build.

## vcpkg

vcpkg is used to acquire and build native dependencies. Each installed port
retains the license of its upstream project.

- Project: <https://github.com/microsoft/vcpkg>
- License: MIT License

## Unity

The Unity integration builds against Unity engine APIs and the Unity native
plug-in interface. Unity itself is not distributed under the OpenVolumetric
license. Users and distributors remain responsible for complying with the
applicable Unity terms for their engine installation and application.

- Product information and terms: <https://unity.com/legal>

## Unreal Engine

The Unreal integration builds against Unreal Engine APIs. Unreal Engine is
not distributed under the OpenVolumetric license. Users and distributors
remain responsible for complying with the applicable Unreal Engine license
and marketplace/distribution terms.

- Product information and license: <https://www.unrealengine.com/eula>

## Operating-system and graphics APIs

OpenVolumetric uses platform SDK APIs including Metal, Vulkan, D3D11, Android,
and operating-system media/framework libraries. Those SDKs and APIs remain
subject to their respective platform terms.

## Media codecs and patents

The software copyright licenses listed above do not grant third-party patent
rights for H.264/AVC, H.265/HEVC, AAC, or other media technologies. Users and
distributors are responsible for determining whether patent licenses or
royalties apply to their use and jurisdiction.
