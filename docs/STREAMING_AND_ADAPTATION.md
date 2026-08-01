# Streaming and adaptive delivery

## Status

Milestone 12 is complete and Milestone 13 adaptive delivery is now in
progress. The runtime has an
engine-independent `IByteSource`, a seekable `LocalFileByteSource`, and FFmpeg
custom I/O beneath the MP4 container. Existing path-based playback is routed
through this boundary. `HttpRangeByteSource` now provides cancellable
HTTP/HTTPS range requests on a dedicated worker with a bounded block cache.
New authoring outputs place `moov` before `mdat` and reject packaging if that
fast-start invariant is absent. Progressive container opening, packet reads,
seeking, looping, and pause/resume have been manually validated over HTTP in
Unity on macOS, Unity on Quest over Wi-Fi, and Unreal Editor on macOS. Unity
and Unreal expose optional HTTP(S) URL fields plus core-owned resource-size,
cache, download, request-count, and transport-state diagnostics. Fixed-quality
fragmented MP4 authoring and whole-resource playback are implemented. The HTTP
source now reads the terminal `mfra`/`tfra` index, schedules complete forward
fragments within its byte budget, and allows demanded demux reads to pre-empt
speculative downloads. The next implementation slice defines the versioned
presentation manifest and engine-neutral representation model before adding
multi-representation authoring and switching.

The bounded fragment scheduler has been manually validated on Quest over
Wi-Fi for uninterrupted playback, forward and backward seeking, and recovery
after a brief network interruption. Network interruption and synchronized
recovery have also passed manual testing in Unreal. Controlled retry
exhaustion and deterministic geometry-packet corruption have subsequently
passed on Quest: retries terminated in the defined error state, corruption was
contained without a crash, and clean input reopened normally.

For current progressive-file testing:

- In Unity, set `videoUrl` on the `OpenVolumetric` component. It takes
  precedence over `videoFilename`; the Quest developer overlay shows HTTP
  cache, downloaded-byte, and request counters.
- In Unreal, set **Source URL** on `UOpenVolumetricComponent`. It takes
  precedence over **Source File**; counters appear under **Status > Buffer**.
- Serve a newly authored fast-start MP4 from an HTTP(S) endpoint that returns
  a stable `Content-Length` and honors byte ranges.

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
- a complete `IndependentMesh` geometry packet that establishes the segment's
  active topology; and
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
- Draco geometry quantization and encode-speed settings; and
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

### Implemented manifest profile: version 1

The first implementation uses a JSON sidecar with format identifier
`openvolumetric-adaptive` and version `1`. It references complete aligned
fragmented-MP4 resources rather than separate initialization and media files,
allowing the adaptive runtime to reuse Milestone 12's validated fragment index
and byte-range cache. This transport syntax is an implementation stepping
stone, not a replacement for the planned MPEG-DASH evaluation.

The top level declares a presentation identifier, total and nominal segment
durations, audio presence, and two or more representations. Each coupled
representation declares:

- a unique identifier and MP4 resource URI;
- a compatibility group;
- aggregate required bandwidth;
- texture codec, dimensions, and nominal bitrate; and
- geometry codec, position precision, nominal bitrate, and temporal-coding
  mode.

`AdaptiveManifestParser` maps this JSON into an engine-neutral model and
rejects unsupported versions, malformed types, invalid durations, duplicate
identifiers, incomplete capability metadata, zero-valued required fields, and
aggregate bandwidth below the declared texture-plus-geometry bitrate. A
two-quality example is available at
[`examples/adaptive-manifest-v1.json`](examples/adaptive-manifest-v1.json).

The shared authoring core now supplies deterministic low/high ladders for the
Desktop Streaming and Quest Streaming presets. Both entries force the selected
1-, 2-, or 4-second fragment duration; the low entry reduces the video bitrate
ceiling and geometry precision while retaining the same codec, timeline, and
random-access cadence.

After both representations are encoded, `write_adaptive_package_manifest`
probes their real MP4 duration, video codec, dimensions, audio presence,
resource bitrate, and terminal fragment index. It rejects missing indexes or
differences in duration, fragment count, or audio layout, then writes the
manifest atomically with measured bitrate and geometry-payload information.
The C authoring ABI exposes the ladder and manifest writer so Unity and Unreal
share this behavior. Both editor integrations orchestrate the two encode
passes and expose the same package layout: authors select an output parent
folder and presentation name, producing
`<parent>/<presentation>/manifest.json`, `low.mp4`, and `high.mp4`. These
stable relative names allow the whole directory to be copied directly to an
HTTP origin without engine-specific renaming.

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

The shared Unity and Unreal authoring pipeline now supports a fixed-quality
fragmented MP4 mode with 1-, 2-, or 4-second durations. It writes one file
containing an initialization `moov` and aligned `moof`/`mdat` fragments,
forces closed video GOPs and independent geometry at every boundary, and
verifies fragment count, box order, access-point alignment, payloads,
timestamps, and seeking.

Fragment muxing also limits consecutive packets from one track. This is a
runtime requirement, not only a file-layout preference: a long video-only run
can fill a bounded video queue before the demuxer reaches the audio or geometry
packets needed for the same presentation. The authoring verifier rejects such
sparse track interleave.

The fixed-representation scheduler discovers fragment byte ranges from the
terminal MP4 random-access index rather than adding top-level `sidx` boxes.
This preserves the unified FFmpeg seek behaviour of the custom geometry track.
It fills the active and following complete fragments up to the configured
32 MB cache budget, evicts old data by recent use, and immediately yields to a
new demanded block after a seek. Conventional non-fragmented MP4 files retain
the smaller sequential block read-ahead path. Core diagnostics report whether
the input is fragmented, total and active fragment indices, and the number of
complete fragments currently cached; Unity and Unreal expose the same values.

The remaining adaptive-package work is to:

1. Validate source duration, timestamps, and frame correspondence.
2. Encode each requested video/geometry quality representation.
3. Reuse the implemented aligned video and geometry random-access rules.
4. Emit independently addressable delivery objects where required.
5. Write the manifest/descriptor.
6. Reopen and verify every initialization/media segment.
7. Verify every representation switch boundary.
8. Produce a size, bitrate, keyframe, and compatibility report.

The current Desktop Streaming and Quest Streaming presets create one
fast-start or optionally fragmented MP4 with bounded texture-video rate and
bounded video/geometry reference windows. A future adaptive packaging mode should extend this
platform-and-delivery model to define a representation ladder, segment
duration, aligned switch points, geometry precision, and decoder-capability
constraints rather than only one output file.

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

### Progressive HTTP validation findings

Quest testing from 29–31 July 2026 confirmed progressive startup,
forward/backward range seeking, pause/resume, repeated looping, and sustained
playback using a streaming-oriented representation. Unreal Editor playback
over the same progressive HTTP path was also manually validated on macOS.
Temporarily disconnecting Quest Wi-Fi did not crash the player. Early tests
failed to recover and could show incomplete visual presentations; after the
recovery changes, playback recovered in a later test but required a manual
pause/play action before it became stable. Fully automatic recovery is
therefore not yet considered validated.

The recovery path now publishes `Rebuffering`, retries transient range
failures with bounded exponential backoff, retains the last complete
synchronized mesh/texture, and stops engine audio. When transport becomes
ready, the engine seeks every stream back to the last presented timestamp and
resumes only from that synchronized access point. Exhausted retries publish
`Error`. Failure injection covering transient HTTP 503 responses, permanent
503 retry exhaustion, and deterministic geometry-packet corruption has passed.
Brief disconnect/reconnect recovery has passed on Quest and Unreal; exhaustion
and corruption were validated on Quest.

### Fixed-quality fragmented MP4 validation findings

Unity testing on 31 July 2026 validated a two-second fragmented output with
topology compression and audio. The complete Editor workflow succeeded, and
the resulting file passed local startup, synchronized texture/geometry/audio
playback, forward and backward seeks across fragment boundaries,
pause/resume, repeated looping, and non-looping end-to-start restart.

The same file passed progressive HTTP startup, forward/backward seeking,
pause/resume, and repeated looping without first downloading the complete
resource. A video/audio-only stream-copy remux played in VLC, while direct
playback of the complete file stops at its first fragment boundary because VLC
does not ignore `vvge` correctly. This limitation and its diagnostic variants
are recorded in
[CONTAINER_COMPATIBILITY.md](CONTAINER_COMPATIBILITY.md). Native structural
verification had already covered one-, two-, and four-second fragment
durations. The same two-second fragmented output subsequently passed playback
validation in Unreal Editor on macOS.

An experiment with FFmpeg's `global_sidx` option was rejected. FFmpeg emitted
one index per video, audio, and custom `vvge` track, but stream-agnostic
backward seeks repeatedly resolved to the first fragment instead of the
requested synchronized fragment. This caused Unity seek/synchronization
failures and unstable conventional-player playback. Fixed-quality authoring
therefore retains ordinary `moof`/`mdat` discovery. The verifier now exercises
forward and backward stream-agnostic seeks so this failure cannot silently
return. A future scheduler needs one explicitly coupled multimodal index or
manifest rather than treating FFmpeg's per-track global indexes as atomic.

## Implementation phases

### Phase 1: Progressive HTTP input

- [x] Define `IByteSource` and retain the local-file implementation.
- [x] Add HTTP range reads through a portable dependency or platform-neutral
  transport boundary.
- [x] Connect FFmpeg custom I/O to the byte source.
- [x] Author fast-start MP4 with metadata available before media payloads.
- Validate startup, seeking, cancellation, and bounded caching.

### Phase 2: Fragmented single-representation VOD

- [x] Author aligned initialization and fragmented-media segments.
- [x] Add the bounded in-file fragment scheduler and cache.
- [x] Play one fixed representation through the existing synchronization core.
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
