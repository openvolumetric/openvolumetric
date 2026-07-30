# Licensing and distribution

This document explains the intended licensing structure for OpenVolumetric
and identifies release tasks for source and binary distributions. It is
general project guidance, not legal advice.

## OpenVolumetric license

OpenVolumetric's original source code, build files, and documentation are
released under the Apache License 2.0, unless a file or directory explicitly
states otherwise.

The complete terms are in the repository's top-level `LICENSE` file.
Attribution information is in `NOTICE`. Apache 2.0 permits commercial and
non-commercial use, modification, redistribution, and integration into
proprietary Unity and Unreal applications, subject to its conditions.

Third-party software retains its own license. Apache 2.0 does not relicense
FFmpeg, Draco, Unity, Unreal Engine, platform SDKs, or media codecs.

No SPDX source-file headers have been added at this stage. The repository-level
license establishes the intended project license; per-file SPDX identifiers
can be considered separately.

## Scope exclusions

Unless accompanied by an explicit license:

- sample captures and datasets are not automatically licensed by Apache 2.0;
- performer likeness, privacy, publicity, music, and source-media rights are
  not granted;
- third-party binaries and generated dependency trees retain their upstream
  licenses; and
- Unity, Unreal Engine, and platform SDK installations are governed by their
  own terms.

Before publishing a sample dataset, add a dataset-specific license and confirm
that all capture, performer, audio, and redistribution permissions allow that
release. CC BY 4.0 is a suitable permissive option only where the project owns
all necessary rights.

## Source release checklist

A source release should contain:

- `LICENSE`;
- `NOTICE`;
- `THIRD_PARTY_NOTICES.md`;
- `docs/LICENSING.md`;
- the complete OpenVolumetric source used for the release;
- `OpenVolumetricNative/vcpkg.json`, including its pinned baseline and
  dependency versions; and
- release notes identifying tested engine and platform versions.

Do not commit or redistribute generated vcpkg package trees as though they
were Apache-licensed OpenVolumetric source.

## FFmpeg configuration

The runtime manifest disables FFmpeg default features and requests only:

- `avcodec`;
- `avformat`; and
- `swresample`.

Release builds must be audited to confirm that FFmpeg was not configured with
`--enable-gpl` or `--enable-nonfree` and that no additional dependency changes
the resulting license. Record the exact FFmpeg version, vcpkg baseline,
features, patches, configure output, and target triplet used for every
published binary.

The Editor authoring workflow invokes an external FFmpeg executable. The
project should not bundle that executable in its initial release. Users may
select their own executable, and its license depends on its build
configuration. In particular, builds providing libx264 or libx265 commonly
carry GPL obligations.

## Prebuilt native binaries

OpenVolumetric currently links FFmpeg statically into native shared libraries.
Publishing those libraries requires more than adding an LGPL notice.

For every released platform binary, prepare a corresponding compliance bundle
containing:

- the exact FFmpeg source used to build it;
- all local patches or a statement that there were none;
- the complete license texts and copyright notices;
- the exact build instructions and configuration;
- OpenVolumetric and any other object/source material needed to rebuild or
  relink the shared library with a modified FFmpeg; and
- clear instructions for replacing the rebuilt native library in the engine
  integration.

The distributed application terms must not prohibit reverse engineering where
the LGPL permits it for debugging modifications to the covered library.

Dynamic FFmpeg linking on desktop platforms may simplify compliance and
library replacement, but it introduces dependency staging and versioning work.
Static linking remains possible only when the complete applicable LGPL
conditions are satisfied.

Because mobile application signing and packaging can complicate replacement
or relinking, obtain qualified legal review before distributing Android,
Quest, iOS, or store-packaged binaries containing statically linked FFmpeg.

## Draco

Draco is licensed under Apache 2.0 and is compatible with OpenVolumetric's
chosen project license. Binary and source distributions must still retain
Draco's copyright, license, and attribution notices.

## HTTP and TLS dependencies

Milestone 12 HTTP range input uses libcurl. The vcpkg configuration currently
builds libcurl with OpenSSL and zlib for portable HTTPS support. These
dependencies retain their respective curl, Apache 2.0, and zlib licenses.
Source and binary distributions must include their license and copyright
notices in `THIRD_PARTY_NOTICES.md` and the release compliance bundle.

Published native binaries statically link these libraries alongside the other
runtime dependencies. Release records must therefore identify the exact
libcurl, OpenSSL, and zlib versions, vcpkg features, target triplet, sources,
patches, and build instructions used for each platform.

## Engine integrations

OpenVolumetric's Unity and Unreal integration source is Apache 2.0. This does
not grant a Unity or Unreal Engine license and does not alter either engine's
terms.

Release archives should contain only files that the project is permitted to
redistribute. Generated engine binaries, SDK files, proprietary engine source,
and marketplace content should not be included unless their terms expressly
permit it.

## Codec patent considerations

Copyright licenses and codec patent licenses are separate. Apache 2.0 covers
patent claims licensable by OpenVolumetric contributors as described in that
license; it does not grant rights from third-party H.264, HEVC, AAC, or other
codec patent holders.

Anyone distributing encoding or playback products should assess applicable
codec patent obligations for the product, distribution territories, use
case, and selected codecs.

## Contribution policy

Under Apache 2.0 section 5, intentionally submitted contributions are
licensed under Apache 2.0 unless explicitly stated otherwise. Before accepting
substantial external contributions, the project should add:

- `CONTRIBUTING.md` confirming that contribution terms;
- a developer certificate of origin sign-off policy or a contributor license
  agreement, if desired by the project owner; and
- a process for rejecting code copied from incompatible sources.

The copyright ownership wording in `NOTICE` should be reviewed if the project
is owned by, assigned to, or released on behalf of a university or other
institution.
