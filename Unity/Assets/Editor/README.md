# Volumetric Video Encoder

Open the authoring window from:

```text
Tools > Volumetric Video > Encoder
```

The encoder accepts:

- a directory of numbered texture images;
- a directory of matching numbered OBJ meshes;
- an optional audio file;
- an output MP4 path.

For example, these two sequences match:

```text
images/000110.png
images/000111.png

geometry/000110.obj
geometry/000111.obj
```

Frame numbers must be contiguous and the image and OBJ sets must match
exactly. Padding and extensions must be consistent within each sequence.

## Pipeline

One click performs the following stages:

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
Editor-only `VolumetricVideoAuthoring` plugin; no `draco_encoder` executable
or path setting is required. The window discovers FFmpeg through `PATH`, or a
standalone executable can be selected without installing it system-wide.

MP4 packaging is called directly through the Editor-only
`VolumetricVideoAuthoring` native library. It is built and staged alongside
the runtime plugin by the normal CMake build.

The Frame Rate field controls how the source image sequence is encoded. It is
not separately reused to synthesize geometry timestamps; packaging reads the
timestamps back from the resulting video track.
