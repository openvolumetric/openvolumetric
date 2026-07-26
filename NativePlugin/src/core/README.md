# VolumetricVideoCore

`VolumetricVideoCore` contains the engine-independent playback pipeline. It
knows how to demux and decode the combined volumetric MP4, but it does not
include Unity or platform graphics headers.

## Runtime data flow

```text
combined MP4
    |
    v
AVDecoderFFMPEG (one demux thread)
    |-- HEVC packets --> bounded staging --> decoded Y/U/V frame queue
    |-- AAC packets  --> bounded staging --> interleaved float PCM ring
    `-- vvge packets --> bounded staging --> compressed-geometry queue
                              |
                              v
                    GeometryDecoderDraco
                    (geometry worker thread)
                              |
                              v
                       decoded-mesh queue
```

The engine integration requests a presentation frame from
`IVolumetricVideo`. A frame is uploaded only when both its video planes and
decoded mesh are available. Unity owns the destination texture and mesh
resources; the Metal or D3D11 integration copies decoded data into those
resources during Unity's render callback.

## Directory responsibilities

- `container/` defines the engine-independent container and packet interfaces,
  the `vvge` sample format, and the bounded cross-thread queue. Its FFmpeg MP4
  implementation owns `AVFormatContext`, discovers and validates tracks, and
  is the only component that reads or seeks the container.
- `media/` consumes owned container packets and performs video/audio decoding
  plus compressed geometry extraction.
- `geometry/` converts compressed Draco payloads into the engine-neutral
  `Mesh` representation.
- `decoding/` contains interfaces and coordinates media, geometry, and
  platform upload components.
- `support/` contains shared utilities such as logging.

## Threading and ownership

- `FFmpegMp4VolumetricContainer` owns `AVFormatContext`; callers interact through
  `IVolumetricContainer` and receive packets whose payload bytes are owned.
- `AVDecoderFFMPEG` owns codec contexts, its decoder packet, decoded video
  frames, compressed geometry frames, and the audio ring buffer.
- The demux thread is the only thread that reads from the container.
- Runtime seek requests from an engine thread are queued synchronously and
  executed between packet reads by the demux thread. The container and FFmpeg
  codec contexts therefore never seek or flush concurrently with decoding.
- Each stream has independent bounded packet staging. Pending audio is drained
  first, so video or geometry backpressure does not immediately stall audio.
  Demuxing pauses only when a stream's staging limit is also exhausted.
- `GeometryDecoderDraco` owns a separate worker that consumes compressed
  geometry and produces meshes.
- Video and geometry queues have fixed capacities and thread-safe
  open/end-of-stream/error states. Queue access is protected by the queue's
  mutex.
- The Unity render thread consumes matching video and mesh frames and performs
  graphics uploads.
- `destroy()` must stop worker threads before releasing their queues or codec
  state.
- Every seek or loop advances a playback generation. Compressed and decoded
  geometry carry that generation, allowing a Draco result that completed
  during a reset to be discarded instead of appearing in the next loop.
- At natural end-of-stream, queued tail frames remain available to consumers.
  The Unity playback clock explicitly seeks all native streams at its loop
  boundary, clearing old video, geometry, and audio state together.

## Container validation

Opening a runtime MP4 requires exactly one video track and exactly one data
track tagged `vvge`. Audio is optional, but only one audio track is supported.
Missing or duplicated tracks fail before decoder initialization. During
demuxing, malformed `VVGF` samples, missing geometry timestamps, read failures,
and queue overflow move the affected queue into its error state. The Unity API
exposes the most recent message so load failures include the specific reason in
the Unity Console.

## File-format boundary

Runtime input is one MP4 containing:

- an HEVC texture-video track;
- a project-specific `vvge` data track containing `VVGF` geometry packets;
- an optional audio track.

Numbered `.drc` files exist only as temporary inputs inside the Unity
authoring workflow. The runtime has no split-file compatibility path.
