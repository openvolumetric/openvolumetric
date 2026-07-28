# OpenVolumetricAuthoring

The authoring layer contains functionality needed to create volumetric MP4
files, kept separate from runtime playback so game builds do not ship encoding
and verification entry points.

## Targets

- `OpenVolumetricAuthoringCore` is the reusable C++ authoring library.
- `OpenVolumetricAuthoring` is the macOS/Windows shared library called by the
  Unity Editor window.
- Unreal's `OpenVolumetricAuthoring` Editor module links
  `OpenVolumetricAuthoringCore` directly.

Unity calls `openvolumetric_authoring_pack()` through the authoring C API;
Unreal calls `openvolumetric::authoring::pack_openvolumetric()` through its
C++ module. MP4 construction and verification therefore have one
implementation.

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
numbered Draco frames. The Unity and Unreal Editor integrations use the same
linked Draco encoder to convert each OBJ to a temporary `.drc` frame, invoke
FFmpeg separately for HEVC or H.264 video and optional AAC audio encoding,
then pass those artifacts to the packer.

## Platform presets

Both Editor encoders provide four content profiles:

- **Desktop Quality** uses HEVC CRF 20, three reference frames, and balanced
  Draco encode/decode speed. It prioritises quality and compression.
- **Quest Balanced** uses HEVC CRF 25 with no B-frames, one reference frame,
  SAO disabled, and Draco decode speed 9. This is the default Quest profile.
- **Quest Performance** uses H.264 CRF 23 with no B-frames, one reference
  frame, reduced geometry quantization, and Draco decode speed 10. It trades
  file size and some geometry precision for lower software decode cost.
- **Custom** exposes codec, CRF, keyframe interval, reference frames, HEVC SAO,
  quantization, and Draco encode/decode speed.

Draco speed values range from 0 (slowest, best compression) to 10 (fastest).
The decode-speed choice changes how Draco encodes the bitstream and can
therefore increase geometry size. Presets do not resize source images or
change the selected source frame rate.

Draco encoding is intentionally part of the Editor-only
`OpenVolumetricAuthoring` target. Runtime players do not expose authoring
entry points.

Future work can move those encoding stages behind this authoring API without
changing the playback core or the volumetric MP4 format.
