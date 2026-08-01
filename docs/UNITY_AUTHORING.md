# OpenVolumetric Encoder

Open the authoring window from:

```text
Tools > OpenVolumetric > Encoder
```

The encoder accepts:

- a directory of numbered texture images;
- a directory of matching numbered OBJ meshes;
- an optional audio file;
- an output MP4 path for fixed-quality encoding, or an output parent folder
  and presentation name for adaptive encoding.

## Adaptive packages

Enable **Adaptive Package** with either **Desktop Streaming** or **Quest
Streaming** to encode a two-quality package. The output is selected as a
parent folder and presentation name. For example, the presentation name
`presentation` produces:

```text
<output-parent>/
└── presentation/
    ├── manifest.json
    ├── low.mp4
    └── high.mp4
```

Adaptive authoring always enables fragmented MP4 and uses the selected 1-,
2-, or 4-second fragment duration. The shared native ladder reduces bitrate
and geometry precision for the low representation while keeping codec,
timeline, audio layout, and random-access cadence compatible with the high
representation.

Both representations are written to a staging directory. The native authoring
core probes their actual duration, video metadata, bitrate, audio layout, and
fragment indexes and refuses to publish the package if they are not aligned.
Only after this check succeeds are the two MP4 files and versioned JSON
manifest moved into the presentation directory.

To play the package, enable **Use Adaptive Manifest** on the
`OpenVolumetric` component, set the local filename or HTTP URL to
`<presentation>/manifest.json`, and choose **Auto**, **Low**, or **High**.
Local resource paths and HTTP URLs are resolved relative to the manifest.
Auto filters the ladder through conservative desktop or Android capability
ceilings, then uses a bounded HTTP throughput probe where applicable. The
**Adaptive Capability Overrides** fields can tighten or raise those limits for
specific hardware tests; zero retains the platform default. Manual Low and
High bypass these limits. Selection occurs once before playback and does not
switch quality while playing.

For example, these two sequences match:

```text
images/000110.png
images/000111.png

geometry/000110.obj
geometry/000111.obj
```

Frame numbers must be contiguous and the image and OBJ sets must match
exactly. Padding and extensions must be consistent within each sequence.

Enable **Geometry Compression** to detect shared topology and emit
position-only updates. Clear it to use the same packet format with every
sample encoded as an independently decodable Draco mesh.

Enable **Limit Geometry Keyframes** to bound a shared-topology window. The
**Maximum Geometry Frames** value includes the complete Draco reference mesh;
for example, `60` permits that keyframe plus at most 59 position updates.
Leave the limit disabled to reuse matching topology until it changes.

## Pipeline

For a normal single representation, one click performs the following stages:

1. Validate the source sequences before creating output.
2. Encode every OBJ as a temporary Draco `.drc` frame.
3. Encode the image sequence as HEVC and add optional AAC audio.
4. Probe the encoded video samples and add each `vvge` geometry sample using
   its matching video's actual presentation timestamp and duration.
5. Reopen the MP4 and verify every geometry payload, timestamp, and a seek.
6. Replace the selected output only after verification succeeds.
7. Remove temporary media and Draco files.

Cancellation terminates the active child process and removes the temporary
workspace. If cancellation is requested during the short native packaging
stage, verification finishes but its output is discarded.

## Authoring tools

The Advanced section shows the external media encoder used by the window:

- `ffmpeg`, with `libx265` and AAC encoding support.

OBJ meshes are encoded by the Draco library linked directly into the
Editor-only `OpenVolumetricAuthoring` plugin; no `draco_encoder` executable
or path setting is required. The window discovers FFmpeg through `PATH`, or a
standalone executable can be selected without installing it system-wide.

The complete Unity workflow was manually validated on macOS ARM64 on
28 July 2026 with 3,627 matched OBJ/JPEG frames and MP3 audio.

MP4 packaging is called directly through the Editor-only
`OpenVolumetricAuthoring` native library. It is built and staged alongside
the runtime plugin by the normal CMake build.

The Frame Rate field controls how the source image sequence is encoded. It is
not separately reused to synthesize geometry timestamps; packaging reads the
timestamps back from the resulting video track.
