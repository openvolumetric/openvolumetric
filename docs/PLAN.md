# Open Volumetric Development Plan

Last updated: 1 August 2026

## Current objective

Implement Milestone 13 adaptive volumetric streaming: define the versioned
presentation manifest, author aligned quality representations, and add
deterministic representation selection and switching without disturbing the
shared audio clock or existing local, progressive, and fixed-fragmented input.

## Decisions

- Use MP4 as the delivery container.
- Keep HEVC for the texture video.
- Keep audio as a normal MP4 audio track.
- Store one versioned geometry packet per timed MP4 metadata sample, using
  either a complete Draco mesh or a Draco position-only update referencing a
  preceding topology keyframe.
- Identify the geometry track with the project-specific `vvge` metadata
  sample entry rather than mislabeling it with GoPro's `gpmd` identifier.
- Synchronize decoded texture and geometry by presentation timestamp, not by
  filenames or Unity frame counters.
- Keep container parsing, decoding, buffering, and synchronization in
  `OpenVolumetricCore`.
- Keep each engine responsible only for its adapter, graphics resources,
  audio output, presentation clock, and lifecycle. Unity and Unreal both use
  the same engine-independent core.
- Require the combined MP4 format at runtime. Numbered `.drc` files are
  temporary Unity authoring inputs, not runtime inputs.

## Proposed MP4 tracks

```text
OpenVolumetric.mp4
├── Video track: HEVC texture
├── Audio track: AAC or another MP4-compatible codec (optional)
└── Timed metadata track: Draco geometry
```

The Milestone 1 sample entry is:

```text
vvge
```

The pinned FFmpeg 8.1.2 MP4 muxer only authors binary data tracks as `gpmd`.
The experimental packer uses FFmpeg to construct and interleave the file and
then changes the equal-sized geometry declarations in `moov` to `vvge`.
Runtime FFmpeg successfully reopens this as a binary data stream. This
representation remains provisional until interoperability testing is complete.

## Geometry sample format

Every metadata sample contains the current 40-byte, big-endian `VVGF` version
2 header followed by either a complete Draco mesh or a sequential Draco point
cloud containing updated positions. The header records coding mode, payload
codec, topology ID, source and referenced-keyframe frame numbers, vertex and
triangle counts, flags, and payload size.

The MP4 sample supplies the authoritative presentation timestamp and duration.
Frame numbers are used only for validation, dependencies, and diagnostics.
See [GEOMETRY_PACKET.md](GEOMETRY_PACKET.md) for the normative field layout.

## Milestone 1: MP4 metadata proof of concept

- [x] Add a small experimental packer under `OpenVolumetricNative/tools`.
- [x] Copy the existing HEVC and audio tracks without re-encoding.
- [x] Add timed geometry metadata samples for the complete sample sequence.
- [x] Reopen the generated MP4 using the project's vcpkg FFmpeg build.
- [x] Confirm the geometry track can be identified unambiguously.
- [x] Confirm every sample retains its exact payload, PTS, duration, and order.
- [x] Confirm `av_seek_frame()` can reach the expected geometry sample.
- [x] Confirm the initial conventional-file fixture plays video and audio in
      selected ordinary players. Fragmented-file limitations are documented
      separately and are not covered by this historical proof-of-concept test.
- [x] Decide whether stock FFmpeg can author the chosen metadata entry.

Stock FFmpeg cannot directly author the chosen `vvge` entry. The current
proof-of-concept performs a size-preserving declaration change inside `moov`.
Continue to evaluate GPAC/MP4Box as a packaging-only dependency if broader
interoperability shows that a standardized generic metadata entry is needed.
Avoid maintaining a patched FFmpeg fork unless the other options are
unsuitable.

### Milestone 1 exit criteria

- A test MP4 contains valid video, optional audio, and recoverable Draco
  samples.
- Round-trip payload comparison succeeds for every test geometry frame.
- Seeking returns the correct geometry sample for several timestamps.
- The selected metadata-track representation is documented and stable enough
  to implement in the runtime.

## Milestone 2: Container and packet abstractions

- [x] Add `src/core/container`.
- [x] Introduce a container interface independent of Unity and Unreal.
- [x] Add an FFmpeg MP4 implementation owning `AVFormatContext`.
- [x] Discover streams by type plus explicit geometry metadata identity.
- [x] Introduce a timestamped compressed geometry-frame type.
- [x] Add bounded, thread-safe queues with explicit end-of-stream state.
- [x] Produce useful errors for missing, duplicated, or malformed tracks.

Proposed source layout:

```text
OpenVolumetricNative/src/core/
├── container/
│   ├── IVolumetricContainer.h
│   ├── FFmpegMp4VolumetricContainer.h
│   ├── FFmpegMp4VolumetricContainer.cpp
│   ├── GeometryPacket.h
│   └── TimedFrame.h
├── decoding/
├── geometry/
├── media/
└── support/
```

## Milestone 3: Unified demux and decoding

- [x] Make one demux thread responsible for `av_read_frame()`.
- [x] Route video packets to the FFmpeg video decoder.
- [x] Route audio packets to the FFmpeg audio decoder.
- [x] Route geometry samples to a Draco worker queue.
- [x] Refactor `GeometryDecoderDraco` to accept encoded bytes rather than
      loading numbered files.
- [x] Preserve packet timestamps through Draco decoding.
- [x] Apply backpressure without starving the audio stream.
- [x] Handle end-of-stream and looping without stale frames.

## Milestone 4: Timestamp synchronization

- [x] Replace independent frame-index lookup with timestamped video frames and
      meshes.
- [x] Pair texture and geometry using PTS and a defined tolerance.
- [x] Define behavior for a missing or late geometry sample.
- [x] Keep audio scheduled from the same playback clock.
- [x] Add diagnostics for dropped, duplicated, or mismatched samples.
- [x] Verify that long playback does not accumulate synchronization drift.

## Milestone 5: Unified seeking

- [x] Pause packet production before seeking.
- [x] Seek the MP4 demuxer to the requested timestamp.
- [x] Flush video and audio codec state.
- [x] Clear compressed and decoded geometry queues.
- [x] Reset the audio ring buffer.
- [x] Decode forward until a complete presentation set is available.
- [x] Test repeated forward, backward, loop-boundary, and near-end seeks.

Because each Draco sample contains a complete mesh, geometry should not require
inter-frame dependency preroll.

## Milestone 6: Unity authoring

- [x] Extract packaging and verification into `OpenVolumetricAuthoringCore`.
- [x] Expose packaging to the Unity Editor through a separate authoring
      library rather than the runtime player plugin.
- [x] Provide the user-facing authoring workflow through the Unity Editor;
      the same authoring core is now also exposed through Unreal Editor.
- [x] Accept numbered images and OBJ meshes plus optional audio.
- [x] Encode OBJ meshes to temporary Draco frames automatically.
- [x] Encode OBJ meshes through the linked Editor-only Draco library without
      invoking the standalone `draco_encoder` application.
- [x] Derive geometry timestamps from the source video rather than assuming a
      hard-coded frame rate where possible.
- [x] Validate frame counts, input gaps, and empty files.
- [x] Add round-trip payload, timestamp, and seek verification.
- [x] Build the authoring library through CMake and make dependencies available in the
      development container.
- [x] Document the Unity authoring workflow.

## Milestone 7: Engine APIs

- [x] Add a core API path that opens one volumetric MP4.
- [x] Add a Unity native path that discovers embedded geometry while loading
      the MP4.
- [x] Update the Unity C# component to use the combined MP4 geometry when
      present.
- [x] Remove the legacy separate geometry runtime path after validating the
      combined format.
- [x] Ensure no Unity types or headers enter `OpenVolumetricCore`.
- [x] Document the core boundary used by the Unreal adapter.

## Milestone 8: Meta Quest 3S Unity player

- [x] Confirm the minimum supported Unity, Android SDK/NDK, OpenXR, and Quest
      OS versions and record them as the platform baseline.
- [x] Add an Android `arm64-v8a` CMake/vcpkg triplet and build
      `OpenVolumetricCore` plus the Unity native plugin as packaged `.so`
      libraries.
- [x] Add a Unity Vulkan rendering backend for native YUV texture upload and
      dynamic mesh-buffer updates. Treat OpenGL ES as an optional fallback,
      not the initial target.
- [x] Package the native libraries and Unity import settings so they are
      included only for compatible Android/ARM64 builds.
- [ ] Validate whether FFmpeg software HEVC decoding meets frame-time,
      thermals, and battery targets on Quest 3S.
- [ ] Introduce a video-decoder backend abstraction and Android MediaCodec
      implementation if hardware HEVC decoding is required.
- [x] Keep audio, geometry, and texture presentation on the existing
      timestamp synchronization clock.
- [x] Build a headset-friendly Unity sample player with open, play, pause,
      loop, seek, status, and error controls.
- [x] Handle Android asset delivery and readable local-file paths without
      assuming desktop `StreamingAssets` filesystem behavior.
- [x] Add Desktop Local, Desktop Streaming, Quest Local, Quest Streaming,
      and Custom authoring presets for video complexity, bounded network
      rate, reference windows, and Draco decode speed.
- [ ] Profile CPU, GPU, memory, queue depth, dropped presentations, thermals,
      and sustained playback on the physical Quest 3S.
- [ ] Produce and test a signed development APK from a clean checkout.

Quest 2 validation on 26 July 2026 confirmed synchronized software-decoded
HEVC texture, Draco geometry, and audio playback through the Vulkan backend.
The controller-operated developer overlay, play/pause, loop, and seek controls
also worked in-headset with generally good performance. This is a useful
lower-tier performance baseline, but it does not replace sustained thermal and
battery profiling or final validation on the target Quest 3S.

A subsequent test lasting more than 10 minutes held the Quest 2 refresh target
at 72 FPS with smooth synchronized playback and no concerning headset
temperature. Looping and seeking were exercised successfully. One mesh stop
was observed while looping was not enabled and is consistent with reaching the
configured non-looping end of stream; repeat the long test with looping
explicitly enabled if it recurs.

Post-rename validation on 27 July 2026 confirmed that the Open Volumetric
runtime still plays successfully through the macOS Metal and Quest Android
Vulkan integrations after the native source tree, plugins, exported API, and
Unity namespaces were renamed to OpenVolumetric.

### Milestone 8 exit criteria

- A clean build installs and launches on Quest 3S.
- A combined MP4 plays texture, synchronized geometry, and audio in-headset.
- Looping and seeking remain stable during sustained playback.
- Performance limits and the selected software or hardware video-decoder path
  are documented with measurements.

## Milestone 9: Unreal Engine integration

- [x] Finish documenting the engine-neutral core ownership, threading,
      presentation, seeking, diagnostics, and graphics-upload boundary.
- [x] Select and record the first supported Unreal Engine version and initial
      desktop target; add Android/Quest only after the desktop adapter is
      stable.
- [x] Create an Unreal plugin with separate runtime and Editor modules,
      native-library declarations, and sample project/content.
- [x] Wrap one core decoder instance in an Unreal-facing component or UObject
      with explicit lifecycle and error reporting.
- [x] Implement dynamic-mesh and transient-texture updates without exposing
      Unreal types inside `OpenVolumetricCore`.
- [ ] Replace the initial CPU YUV conversion and texture update with an
      RHI-optimized upload path where profiling justifies it.
- [x] Feed decoded PCM through Unreal's procedural audio path using the same
      presentation clock as texture and geometry.
- [x] Map Unreal play, pause, loop, and seek commands onto the unified core
      APIs without allowing engine threads to mutate decoder state directly.
- [ ] Support Unreal packaging for the initial target, including FFmpeg,
      Draco, runtime-library staging, and architecture selection.
- [x] Add an Unreal sample actor and level demonstrating synchronized
      geometry, texture, and audio playback.
- [x] Add the OpenVolumetric authoring workflow to the Unreal Editor.
- [x] Validate a complete 3,627-frame OBJ, JPEG, and MP3 authoring run through
      the Unreal Editor on macOS ARM64.
- [x] Validate basic synchronized playback in Unreal Editor 5.8 on macOS.
- [ ] Validate packaged builds, repeated lifecycle creation and destruction,
      looping, seeking, and missing/corrupt input handling.

### Milestone 9 exit criteria

- The same volumetric MP4 plays through Unity and Unreal without format
  conversion.
- Unreal integration code depends only on the public core boundary.
- Editor and packaged playback pass synchronization, loop, seek, and
  lifecycle tests on the selected first platform.

## Milestone 10: Topology-aware geometry format

The detailed representation, fallback behavior, decoding cache,
seeking rules, validation matrix, and phased implementation are recorded in
[TOPOLOGY_COMPRESSION.md](TOPOLOGY_COMPRESSION.md).

- [x] Define a stable topology fingerprint covering vertex/index counts,
      index order, attribute presence, and attribute mapping.
- [ ] Analyse representative sequences and report runs where topology is
      constant, including whole-sequence and short-window distributions.
- [x] Define three geometry coding strategies: independent mesh, window keyframe
      plus dependent frames, and one topology shared by an entire sequence.
- [x] Propose a versioned geometry sample header with coding mode,
      dependency/keyframe information, topology identifier, and decoded-size
      validation.
- [x] Define how positions, normals, UVs, and any future attributes are
      quantized and predicted when topology is reused.
- [x] Select a delta/residual compression backend using measured compression
      ratio, decode cost, memory use, implementation complexity, and licensing
      rather than assuming Draco is optimal for temporal residuals.
- [x] Define random-access rules and maximum dependency-window length so MP4
      seeks can begin from a known geometry keyframe.
- [x] Define recovery behavior for missing, corrupt, or mismatched dependent
      samples.
- [x] Build an offline comparison tool or internal test harness that reports
      bytes per frame, compression ratio, encode/decode time, and geometric
      reconstruction error against independent Draco.

### Milestone 10 exit criteria

- The format and dependency rules are documented before runtime integration.
- Benchmarks identify when independent, windowed, and whole-sequence modes
  should be selected.
- Independent and topology-reusing samples use the same packet format.

## Milestone 11: Temporal geometry encoding and playback

- [x] Add authoring analysis that automatically segments input into
      same-topology windows and selects independent, windowed, or
      whole-sequence coding.
- [x] Encode a full topology/keyframe at every required random-access point
      and temporally compressed attribute updates for dependent samples.
- [x] Add a configurable maximum geometry keyframe interval. Force a complete
      Draco reference mesh when that many geometry samples have elapsed,
      even if topology and UVs remain unchanged.
- [x] Expose the optional maximum geometry keyframe interval in both Unity and
      Unreal authoring, with positive-value validation when enabled.
- [x] Package dependency metadata and compressed payloads into the existing
      timestamped `vvge` MP4 track.
- [x] Extend the core geometry decoder with topology caches and dependent-frame
      reconstruction without adding Unity or Unreal dependencies.
- [x] Bound topology-cache, compressed-update, and decoded-mesh memory.
- [x] Integrate seek preroll from the preceding geometry keyframe with the
      unified seek pipeline.
- [ ] Preserve timestamp matching, EOS generations, corruption handling, and
      dropped-sample diagnostics for dependent geometry.
- [x] Add Unity and Unreal controls for automatic topology reuse, an optional
      maximum dependency window, and preset/custom Draco quantization.
- [ ] Add an explicit quality/error target and forced-independent frame
      markers if evaluation shows that authors need them.
- [x] Verify independent fallback when topology changes unexpectedly.
- [ ] Run visual, numeric, compression-ratio, long-playback, loop, and seek
      tests on desktop and Quest-class hardware.

### Milestone 11 exit criteria

- Same-topology samples reconstruct within the configured error target.
- Windowed streams remain seekable and recover at the next geometry keyframe
  after a corrupt or missing dependent sample.
- No dependent geometry sample references a keyframe farther back than the
  configured maximum geometry keyframe interval.
- Representative content is materially smaller than independent-frame Draco
  without violating playback performance budgets.

## Milestone 12: Streamable input and fragmented packaging

**Status: complete (31 July 2026).**

The complete progressive-download, fragmented-MP4, adaptive-selection,
threading, recovery, authoring, and validation design is recorded in
[STREAMING_AND_ADAPTATION.md](STREAMING_AND_ADAPTATION.md).

- [x] Add an engine-independent byte/segment source beneath the MP4
      container while retaining local-file input.
- [x] Add cancellable HTTP byte-range input and bounded caching.
- [x] Author fast-start MP4 files for progressive playback.
- [x] Connect FFmpeg custom I/O to the network byte source.
- [x] Validate progressive startup and seeking without downloading the whole
      asset.
- [x] Author initialization segments and aligned fragmented-MP4 media
      segments for a single fixed-quality representation.
- [x] Add a bounded segment scheduler/cache with explicit opening, buffering,
      playing, rebuffering, ended, and error states.
- [x] Ensure video and geometry random-access points align with every
      independently addressable segment.
- [x] Align topology keyframes with segment boundaries when temporal geometry
      coding is used.
- [x] Resolve or formally document ordinary-player interoperability for
      fragmented files containing the custom `vvge` track. VLC currently stops
      at the first fragment boundary, although equivalent fragmented
      video/audio-only remuxes play. The limitation, diagnostic variants,
      preview-remux workflow, and future standardized timed-metadata options
      are documented in
      [CONTAINER_COMPATIBILITY.md](CONTAINER_COMPATIBILITY.md).
- [x] Expose URL input and buffer diagnostics through the core API, Unity, and
      Unreal without placing transport logic in either engine adapter.
- [x] On a network outage, enter a defined rebuffer/error state, freeze the
      last complete presentation, silence audio, and recover from a valid
      synchronized access point after connectivity returns.
- [x] Validate outage recovery and synchronized resume on Quest and Unreal,
      plus controlled retry exhaustion and corruption handling on Quest.

### Milestone 12 exit criteria

- [x] Local-file playback remains unchanged.
- [x] Playback can start and seek over HTTP without first caching the complete
  conventional MP4.
- [x] A fixed fragmented representation plays across every segment boundary with
  synchronized texture, geometry, and optional audio.
- [x] Conventional video and audio remain extractable without re-encoding, and
  generic-player limitations are documented with a preview workflow.
- [x] Network cancellation, retries, cache limits, seeking, looping, and corrupt
  segment handling pass controlled tests.

## Milestone 13: Adaptive volumetric streaming

**Status: in progress (1 August 2026).**

- [x] Define a versioned manifest/profile describing representations,
      compatibility groups, segment timelines, codecs, decoder requirements,
      and geometry precision.
- [x] Author at least two aligned quality representations from one source in
      Unity and Unreal, with byte-identical representation output and
      equivalent manifests.
- [ ] Evaluate MPEG-DASH as the initial manifest/delivery model and document
      custom `vvge` signaling and interoperability limits.
- [x] Implement startup representation selection with Auto, Low, and High
      choices in Unity and Unreal. Auto currently selects the highest declared
      bandwidth; live throughput-based changes remain separate work.
- [ ] Implement conservative throughput-based automatic selection constrained
      by device and measured decode capability.
- [ ] Switch compatible texture and geometry atomically at aligned segment
      boundaries while preserving the audio clock.
- [ ] Cancel and isolate stale in-flight downloads using playback generations.
- [ ] Add Unity and Unreal buffer/quality controls and developer diagnostics.
- [ ] Test bandwidth ramps, latency, jitter, outages, failed upgrades,
      repeated switching, seeking, looping, and long playback.
- [ ] Measure startup delay, rebuffering, quality, wasted bytes, memory,
      synchronization error, and decode cost on desktop and Quest hardware.

### Milestone 13 exit criteria

- Quality changes do not produce mixed incompatible texture/geometry
  presentations or synchronization discontinuities.
- The player steps down and recovers under constrained or interrupted
  bandwidth without deadlock or unbounded memory growth.
- The authoring verifier validates all segment and representation switch
  boundaries.
- Local, progressive, fixed-fragmented, and adaptive inputs share the same
  core decoding and presentation architecture.

## Test matrix

- [x] MP4 with HEVC, AAC, and geometry.
- [x] MP4 with HEVC and geometry but no audio.
- [ ] First, middle, and final geometry samples decode correctly.
- [x] Payload corruption produces a controlled error.
- [ ] Missing geometry samples follow documented behavior.
- [ ] Geometry and video frame-count mismatch is detected by the packer.
- [x] Continuous playback remains synchronized.
- [x] Looping remains synchronized.
- [x] Seeking remains synchronized.
- [ ] Windows D3D11 Unity plugin.
- [x] macOS Metal Unity plugin.
- [ ] Linux core/container tests in Docker.
- [x] Android ARM64 core and Unity native-plugin build.
- [ ] Quest 3S Vulkan playback, looping, seeking, thermals, and sustained load.
- [x] Unreal Editor playback on macOS.
- [x] Unity Editor end-to-end authoring on macOS ARM64.
- [x] Unreal Editor end-to-end authoring on macOS ARM64.
- [ ] Unreal packaged-build playback on the selected first target.
- [x] Independent geometry uses the unified packet format.
- [ ] Windowed same-topology geometry seeks from every keyframe boundary.
- [ ] Whole-sequence topology reuse survives long playback and looping.
- [ ] Corrupt dependent geometry recovers at the next keyframe.
- [x] Progressive HTTP startup and byte-range seeking in Unity on macOS and
      Quest, and in Unreal Editor on macOS.
- [x] Fixed-quality fragmented MP4 playback across segment boundaries in
      Unity, locally and over progressive HTTP.
- [ ] Adaptive switching under controlled bandwidth, latency, and outages.
- [x] Adaptive-manifest startup selection validated locally and over HTTP at
      Low and High quality in Unity and Unreal, including seeking,
      pause/resume, looping, and synchronized audio/texture/geometry playback.
- [ ] Texture and geometry remain compatible and synchronized after every
      representation change.

## Later work

- Consider optional checksums for geometry samples.
- Consider low-latency live ingest after adaptive on-demand playback is
  stable.
- Formalize the metadata MIME type and sample format if files need to
  interoperate with software outside this project.
