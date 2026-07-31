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

## Geometry coding

The packer accepts an already encoded video/audio MP4 and a directory of
numbered Draco frames. The Editor integrations also provide the matching OBJ
directory so the shared packer can detect topology windows.

When compression is enabled, temporary full meshes use Draco's sequential
mesh mode, preserving canonical vertex and index ordering. The first frame of
each topology window becomes a complete-mesh keyframe. Matching subsequent
frames become sequential Draco point clouds containing positions only.

Both Editor encoders provide an optional **Limit Geometry Keyframes** control
and **Maximum Geometry Frames** value. For a value of `N`, the encoder emits
one full Draco reference mesh followed by at most `N - 1` position updates
before forcing another full reference mesh. Disabling the limit allows reuse
until topology changes. This is separate from the video codec's keyframe
interval and bounds geometry seek/preroll cost for streamed content. Topology
changes or media-segment boundaries can force an earlier geometry keyframe.

The packer rejects a keyframe whose decoded indices differ from the canonical
OBJ, preventing an order mismatch from corrupting dependent updates.

Both Editor windows expose a **Geometry Compression** control. Enabled emits
topology keyframes and position updates. Disabled uses default Draco mesh
encoding and emits independently decodable geometry packets in the same
format.

## Fragmented MP4

Unity and Unreal expose an optional **Fragmented MP4** mode with 1-, 2-, or
4-second fragment durations. The selected duration must contain an integral
number of source frames. When enabled, the shared workflow:

- forces a closed video GOP at each fragment boundary and disables scene-cut
  keyframes;
- forces an independent Draco mesh at the same boundary, even when topology
  could otherwise be reused across it;
- writes an initialization `moov` followed by `moof`/`mdat` media fragments
  in the output MP4;
- limits consecutive samples from one track so video cannot starve audio or
  geometry in bounded runtime queues; and
- verifies fragment count, initialization-box order, aligned video/geometry
  access points, every geometry payload and timestamp, and representative
  seeking before publishing the output.

The result remains one `.mp4` file. This phase establishes independently
decodable fixed-quality fragments; a later segment scheduler may address
those fragments individually or package them as separate delivery objects.
Conventional fast-start MP4 remains the default.

## Platform presets

Both Editor encoders provide five content profiles:

- **Desktop Local** uses HEVC CRF 20, three reference frames, and balanced
  Draco encode/decode speed. It prioritises quality and compression without
  imposing a network-rate ceiling.
- **Desktop Streaming** uses HEVC CRF 23, a 16 Mbps texture-video ceiling,
  a 32 Mbps encoder buffer, two reference frames, 60-frame video GOPs, and
  60-frame geometry reference windows.
- **Quest Local** uses H.264 CRF 23 with no B-frames, one reference frame,
  reduced geometry quantization, and Draco decode speed 10. It prioritises
  reliable headset decoding without imposing a network-rate ceiling.
- **Quest Streaming** uses HEVC CRF 27, an 8 Mbps texture-video ceiling,
  a 16 Mbps encoder buffer, one-second video keyframes, and one-second
  geometry reference windows. It prioritises stable Quest Wi-Fi delivery,
  bounded startup/seek work, and fast Draco decoding.
- **Custom** exposes codec, CRF, video keyframe interval, reference frames,
  HEVC SAO, quantization, and Draco encode/decode speed. The optional geometry
  keyframe limit is available independently of the selected preset.

Draco speed values range from 0 (slowest, best compression) to 10 (fastest).
The decode-speed choice changes how Draco encodes the bitstream and can
therefore increase geometry size. Presets do not resize source images or
change the selected source frame rate. Streaming bitrate ceilings apply to
the texture-video track; geometry and audio still contribute to the total
container bitrate.

Draco encoding is intentionally part of the Editor-only
`OpenVolumetricAuthoring` target. Runtime players do not expose authoring
entry points.

The remaining external stage is FFmpeg texture/audio encoding. It can move
behind a future authoring API without changing the playback core or geometry
packet format.

## Validation

On 28 July 2026, the Unity and Unreal Editor authoring front ends were both
validated on macOS ARM64 using the same complete source set:

- 3,627 numbered OBJ meshes (`000110` through `003736`);
- 3,627 matching 1024x1024 JPEG texture frames;
- one MP3 audio source; and
- the pinned FFmpeg 8.1.2 authoring executable with `libx264`, `libx265`, and
  AAC encoding support.

Both front ends completed OBJ-to-Draco encoding, texture/audio encoding, MP4
packaging, and output verification through the shared authoring core.

On 31 July 2026, the native authoring verifier also produced and reopened a
150-frame, 30 fps topology-reuse fixture using all supported fragment sizes:
five 1-second fragments, three 2-second fragments, and two 4-second fragments.
The temporal case retained position updates between mandatory boundary
keyframes and reported a 77.23% geometry-payload reduction for that fixture.
Unity subsequently authored a two-second fragmented sequence with geometry
compression and audio. Local and progressive HTTP playback passed initial
startup, synchronization, forward/backward boundary seeks, pause/resume,
looping, and non-looping restart tests. Its conventional video and audio were
also verified independently. Direct VLC playback of the complete fragmented
file is a documented `vvge` compatibility limitation; an A/V-only stream-copy
remux plays correctly without re-encoding.
The two-second fragmented output also played successfully in Unreal Editor on
macOS and in Unity on Quest/Android.
