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
    |-- HEVC packets --> decoded Y/U/V video-frame queue
    |-- AAC packets  --> interleaved float PCM ring buffer
    `-- vvge packets --> validated compressed-geometry queue
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

- `container/` defines the binary payload stored in each `vvge` MP4 sample.
  It validates framing but does not decode Draco.
- `media/` owns FFmpeg, stream discovery, demuxing, video/audio decoding, and
  compressed geometry extraction.
- `geometry/` converts compressed Draco payloads into the engine-neutral
  `Mesh` representation.
- `decoding/` contains interfaces and coordinates media, geometry, and
  platform upload components.
- `support/` contains shared utilities such as logging.

## Threading and ownership

- `AVDecoderFFMPEG` owns its FFmpeg contexts, packet, decoded video frames,
  geometry packets, and audio ring buffer.
- Its demux thread is the only thread that calls `av_read_frame()`.
- `GeometryDecoderDraco` owns a separate worker that consumes compressed
  geometry and produces meshes.
- Queue access is protected by the mutex belonging to its owner.
- The Unity render thread consumes matching video and mesh frames and performs
  graphics uploads.
- `destroy()` must stop worker threads before releasing their queues or codec
  state.

## File-format boundary

Runtime input is one MP4 containing:

- an HEVC texture-video track;
- a project-specific `vvge` data track containing `VVGF` geometry packets;
- an optional audio track.

Numbered `.drc` files exist only as temporary inputs inside the Unity
authoring workflow. The runtime has no split-file compatibility path.
