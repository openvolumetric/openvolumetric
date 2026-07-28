# Streaming and adaptive delivery

## Status

This document records a proposed future extension to OpenVolumetric. Network
streaming, fragmented MP4, and adaptive representation switching are not
implemented in the current runtime.

The goal is to play OpenVolumetric content before the complete asset has been
downloaded and to adapt texture and geometry quality to available bandwidth,
decoder capacity, and device performance while preserving synchronized audio,
texture, and geometry.

## Terminology and scope

Two delivery modes should be treated separately:

1. **Progressive/range playback** reads one conventional MP4 over HTTP using
   byte-range requests. This is the smallest change and preserves the existing
   file format, but it cannot change quality during playback.
2. **Adaptive segmented playback** uses an initialization segment, short
   fragmented-MP4 media segments, a manifest, and multiple aligned
   representations. This permits quality changes and better recovery from
   network variation.

An adaptive asset is therefore a package rather than one physical MP4:

```text
manifest
├── initialization segments
├── representation 0 media segments
├── representation 1 media segments
└── optional additional representations
```

Each representation still uses the OpenVolumetric MP4 track model. Ordinary
video/audio compatibility should be retained where the selected packaging
standard and player permit it.

Initial scope should be on-demand playback. Low-latency live ingest and live
authoring should follow only after segmented VOD is stable.

## Design principles

- Keep transport, HTTP, manifests, and adaptation out of Unity- and
  Unreal-specific code.
- Preserve MP4 presentation timestamps as the synchronization authority.
- Never present texture from one segment/representation with incompatible
  geometry from another.
- Align all representation boundaries and random-access points.
- Bound network, compressed-packet, decoded-frame, and topology-cache memory.
- Retain local-file playback and independent geometry coding.
- Make representation changes deterministic and observable.
- Recover from slow, missing, or corrupt segments without blocking an engine
  thread.
- Measure end-to-end quality and decoder cost, not bandwidth alone.

## Proposed delivery format

Fragmented MP4/CMAF-style packaging is the preferred media structure:

```text
Initialization segment
├── file and movie metadata
├── texture-video track declaration
├── `vvge` geometry-track declaration
└── optional audio-track declaration

Media segment
├── `moof` fragment metadata
└── `mdat`
    ├── video access unit(s)
    ├── geometry sample(s)
    └── audio sample(s)
```

The initial experiment should evaluate MPEG-DASH because its adaptation-set
and representation model is a natural fit for synchronized nonstandard media
components. HLS support may be added later, but interoperability of the custom
`vvge` track and its signaling must be verified rather than assumed.

The custom geometry sample entry and geometry packet format remain
OpenVolumetric-specific. CMAF terminology here describes fragmentation and
delivery structure; it does not by itself make `vvge` a standardized CMAF
media type.

## Segment boundaries and random access

Every representation must use the same:

- presentation timeline;
- segment start and end timestamps;
- video random-access points;
- geometry random-access points;
- audio timeline origin; and
- declared duration.

A segment must begin with everything required to decode and present it without
loading an earlier media segment:

- an independently decodable video access point;
- an independent Draco geometry packet in the current format; or
- a topology keyframe when topology-aware compression is enabled; and
- sufficient audio preroll or an explicitly documented audio boundary.

This rule intentionally connects adaptive streaming with topology compression:
topology dependency windows must not cross independently addressable segment
boundaries unless the required topology is repeated in the new segment.

Segment duration should be configurable. Short segments improve startup,
switching, and recovery but increase request and container overhead. Initial
evaluation should compare approximately 1, 2, and 4 second segments.

## Representation model

Representations should describe both network cost and decode cost. A useful
ladder may vary:

- texture resolution;
- texture bitrate and codec profile;
- video decoder complexity;
- geometry quantization precision;
- independent versus topology-reused geometry;
- topology-keyframe interval;
- geometry residual compression level; and
- possibly geometry frame rate.

The first adaptive implementation should switch a **coupled presentation
representation** containing compatible texture and geometry. Audio can remain
in a separate, independently selected adaptation set because its bandwidth is
small and its continuity requirements differ.

Later, texture and geometry may be selected independently only if the manifest
explicitly declares compatibility. Independent selection must never rely on
matching representation names or array indices.

If every quality level uses identical geometry, the package may reference one
shared geometry representation from several texture representations. This is
an authoring optimization and must not be assumed for arbitrary assets.

## Manifest metadata

The manifest or an OpenVolumetric sidecar descriptor must provide:

- format/profile version;
- initialization- and media-segment URLs or templates;
- representation identifier;
- codecs and decoder requirements;
- texture dimensions and nominal bitrate;
- geometry mode, precision, and nominal bitrate;
- segment timeline and duration;
- compatibility group for texture/geometry pairing;
- random-access and topology-keyframe guarantees;
- total presentation duration;
- optional hashes or integrity metadata; and
- optional recommended device/profile labels.

The runtime should parse this into an engine-neutral presentation model.
Unity and Unreal receive playback state and quality controls through their
adapters, not raw DASH/HLS objects.

## Core runtime architecture

Add an input boundary beneath the container:

```text
OpenVolumetricPlayer
        |
        v
AdaptivePresentation
├── Manifest parser
├── Adaptation controller
├── Segment scheduler
└── Segment/cache state
        |
        v
IByteSource / ISegmentSource
├── Local file
├── HTTP byte range
└── HTTP media segments
        |
        v
MP4 demux and existing decode/synchronization pipeline
```

The existing local-file FFmpeg container should remain available. Network
support should not be implemented by making engine code download temporary
files without core awareness, because that hides buffering, cancellation,
seek, and error state from the player.

Two implementation approaches should be prototyped:

1. FFmpeg custom I/O over `IByteSource` for progressive/range input.
2. A segment scheduler that supplies complete initialization/media fragments
   to a fragment-capable demux path.

The selected interface must support cancellation, bounded reads, explicit
end-of-resource, retryable failures, seek/range capability discovery, and
read deadlines.

## Buffering and threading

Network I/O must not run on the engine game/render thread or on the demux
thread while that thread is holding decoder state.

Proposed ownership:

- network worker(s): download and validate requested byte ranges/segments;
- scheduler/adaptation worker: chooses requests and maintains the buffer
  target;
- demux owner: reads only available ordered media data and retains sole
  ownership of FFmpeg demux/seek operations;
- existing video/audio/geometry decode workers: decode routed packets;
- engine thread: consumes synchronized presentations and submits controls.

Use bounded queues and a byte-budgeted segment cache. Cancellation and
playback generations must cover in-flight downloads as well as compressed and
decoded frames so a seek or representation switch cannot publish stale data.

Buffer state should be explicit:

```text
Opening -> Buffering -> Playing -> Rebuffering -> Playing
                    \-> Ended
                    \-> Error
```

The engine adapters should expose buffer duration, selected representation,
estimated throughput, recent stalls, and download errors for diagnostics.

## Adaptive selection

Begin with a conservative throughput-based algorithm:

1. Measure completed segment throughput using a smoothed estimator.
2. Reserve a safety margin rather than selecting at the measured limit.
3. Restrict candidates by device capabilities and measured decode cost.
4. Step down promptly when the forward buffer approaches a low watermark.
5. Step up only after sustained headroom and sufficient buffered duration.
6. Apply the change at the next aligned segment/random-access boundary.

Bandwidth is not the only constraint. A Quest-class device may have enough
network throughput for a representation whose HEVC or geometry decode cost is
unsustainable. Each representation therefore needs a capability/complexity
description, and runtime frame timing may place an upper bound on selection.

Manual quality selection should also be supported for testing and user
control. `Automatic`, a fixed representation, and a maximum-quality cap are
useful initial modes.

## Switching behavior

A switch is committed only when the complete compatible target segment is
available. The player should:

1. Continue presenting the old representation while downloading the target.
2. Validate the target segment and its compatibility group.
3. Demux/decode from its aligned random-access boundary.
4. Discard stale-generation results from abandoned requests.
5. Present target texture and geometry together at the first valid timestamp.
6. Preserve the audio clock unless discontinuity metadata requires a reset.

The initial implementation should not mix frames across quality levels within
a segment. Failed upgrades continue on the current representation; inability
to sustain the current level triggers a lower-quality request and may enter
`Rebuffering` if no presentation is available.

## Seeking

Seeking should:

1. Cancel obsolete segment requests.
2. Advance the playback generation.
3. Resolve the target timestamp to a segment and preceding required random
   access point.
4. Select an appropriate representation using current capability and
   bandwidth state.
5. Request its initialization data and target segment.
6. Reuse valid cached segments where possible.
7. Flush and decode forward using the existing unified seek rules.

Topology-compressed geometry must seek from a topology keyframe at or before
the target, preferably at the segment boundary.

## Authoring and packaging

The authoring pipeline should gain a streaming-package mode:

1. Validate source duration, timestamps, and frame correspondence.
2. Encode each requested video/geometry quality representation.
3. Force aligned video and geometry random-access points.
4. Fragment and segment the interleaved media.
5. Write the manifest/descriptor.
6. Reopen and verify every initialization/media segment.
7. Verify every representation switch boundary.
8. Produce a size, bitrate, keyframe, and compatibility report.

Presets can extend the existing Desktop, Quest Balanced, and Quest
Performance model. A preset should define a ladder, segment duration, codec
settings, geometry precision, and decoder-capability constraints rather than
only one output file.

Packaging may continue to invoke the FFmpeg executable for media encoding
while using `OpenVolumetricAuthoringCore` for geometry encoding, muxing,
metadata, and verification.

## Reliability and security

The network path must treat all remote data as untrusted:

- validate manifest sizes, timescales, URLs, and representation counts;
- bound segment, sample, decoded-frame, and allocation sizes;
- reject path traversal and unsupported URL schemes;
- use HTTPS in production;
- apply retry limits, exponential backoff, and request timeouts;
- validate MP4 and `VVGF`/future geometry packet sizes before allocation;
- optionally verify segment hashes;
- distinguish retryable network errors from invalid media; and
- avoid logging credentials or signed query parameters.

CDN authentication and DRM are outside the initial scope, but interfaces
should permit caller-supplied request headers or URL resolution without
embedding engine credentials in the core.

## Validation

Required functional cases:

- progressive startup before full download;
- range seek near the start, middle, and end;
- segmented startup at every media segment;
- automatic and manual representation switching;
- repeated up/down switching;
- switch coincident with topology change;
- seek immediately before, during, and after a switch;
- looping across the final/first segment boundary;
- missing, delayed, corrupt, truncated, and duplicated segments;
- HTTP 404, timeout, disconnect, and reconnect;
- audio absent;
- topology-independent and topology-reused geometry;
- local-file behavior unchanged; and
- Unity and Unreal adapters producing equivalent results.

Network simulation should cover bandwidth ramps, latency, jitter, packet
loss, and temporary outages. Tests should report:

- startup delay;
- rebuffer count and duration;
- quality-switch count;
- selected quality over time;
- downloaded but unused bytes;
- forward-buffer duration;
- end-to-end memory;
- texture/geometry timestamp mismatch;
- dropped presentations;
- decode time by stream; and
- visual/geometric quality.

## Implementation phases

### Phase 1: Progressive HTTP input

- Define `IByteSource` and retain the local-file implementation.
- Add HTTP range reads through a portable dependency or platform-neutral
  transport boundary.
- Connect FFmpeg custom I/O to the byte source.
- Author fast-start MP4 with metadata available before media payloads.
- Validate startup, seeking, cancellation, and bounded caching.

### Phase 2: Fragmented single-representation VOD

- Author aligned initialization and fragmented-media segments.
- Add the segment scheduler and cache.
- Play one fixed representation through the existing synchronization core.
- Validate every segment as an independent access boundary.

### Phase 3: Adaptive package and switching

- Define the manifest/profile and compatibility metadata.
- Author at least two aligned texture/geometry representations.
- Add conservative automatic selection and manual override.
- Switch atomically at aligned segment boundaries.

### Phase 4: Engine integration

- Expose URL/manifest input, buffer state, quality selection, and diagnostics
  through the public core API.
- Add corresponding Unity and Unreal properties/events without duplicating
  transport logic.
- Add development overlays for buffer and representation state.

### Phase 5: Topology-compression integration

- Align topology keyframes and dependency windows with segment boundaries.
- Evaluate shared versus per-representation geometry.
- Validate seek and recovery across every topology/quality combination.

### Phase 6: Evaluation and hardening

- Test desktop, Quest-class, and packaged-engine builds under controlled
  network conditions.
- Tune buffer thresholds, segment duration, ladders, and adaptation safety
  margins from measured results.
- Add conformance fixtures and long-duration recovery tests.

## Recommended initial choices

- Implement on-demand progressive/range playback first.
- Use fragmented MP4 with approximately 2-second aligned segments for the
  first segmented prototype.
- Evaluate MPEG-DASH first while keeping manifest parsing behind an
  engine-neutral interface.
- Switch texture and geometry as one compatible representation initially.
- Keep audio independently selectable but synchronized to the same timeline.
- Require a video and geometry random-access point at every segment boundary.
- Use a conservative throughput algorithm with decode-capability limits.
- Preserve local-file playback as a first-class input mode.
- Treat live streaming, DRM, peer-to-peer delivery, and CDN-specific features
  as later work.
