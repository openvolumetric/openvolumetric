# VolumetricVideoAuthoring

The authoring layer contains functionality needed to create volumetric MP4
files, kept separate from runtime playback so game builds do not ship encoding
and verification entry points.

## Targets

- `VolumetricVideoAuthoringCore` is the reusable C++ authoring library.
- `VolumetricVideoAuthoring` is the macOS/Windows shared library called by the
  Unity Editor window.

The Unity Editor calls `pack_volumetric_video()` through the authoring C API,
so MP4 construction and verification have one implementation.

## Current boundary

The library accepts an already encoded video/audio MP4 and a directory of
numbered Draco frames. The Unity Editor currently performs image/audio and
OBJ/Draco encoding first, then passes those temporary artifacts directly to
the authoring library.

Future work can move those encoding stages behind this authoring API without
changing the playback core or the volumetric MP4 format.
