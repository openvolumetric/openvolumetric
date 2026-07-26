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

## Geometry timing

The packer probes every encoded video sample and assigns the matching geometry
sample that video's actual presentation timestamp and duration. It does not
synthesize geometry timing from an assumed constant frame rate. This preserves
non-zero start times and variable frame timing, and also provides reliable
video/geometry frame-count validation when the container does not populate
`nb_frames`.

Output verification independently reads the packaged video and geometry
tracks, compares every PTS and duration after time-base conversion, verifies
payload identity, and performs a middle-sample seek.

## Current boundary

The packer accepts an already encoded video/audio MP4 and a directory of
numbered Draco frames. The Unity Editor uses the same authoring library's
linked Draco API to convert each OBJ to a temporary `.drc` frame, invokes
FFmpeg separately for HEVC/AAC encoding, then passes those artifacts to the
packer.

Draco encoding is intentionally part of the Editor-only
`VolumetricVideoAuthoring` target. Runtime players do not expose authoring
entry points.

Future work can move those encoding stages behind this authoring API without
changing the playback core or the volumetric MP4 format.
