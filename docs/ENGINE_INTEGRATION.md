# Engine Integration Boundary

This document defines the source-level boundary between
`OpenVolumetricCore` and an engine adapter. It describes the contract
implemented by the Unity and Unreal integrations. It is not yet a stable
binary ABI.

## Boundary

`OpenVolumetricCore` owns:

- MP4 discovery, demuxing, and demux-thread-owned seeking;
- FFmpeg video and audio codec state;
- bounded compressed and decoded frame queues;
- Draco decoding and playback-generation handling;
- timestamp matching policy and seek preroll;
- engine-neutral video, audio, and mesh data.

An engine adapter owns:

- the playback clock and user-facing playback state;
- graphics resources and all render-API calls;
- the audio object that pulls interleaved PCM;
- engine object/component lifetime;
- file deployment and conversion to a readable local path;
- scheduling work onto the engine's game, render, and audio threads.

Unity and Unreal headers or types must not be added to `src/core`. Unity's
native graphics integration lives below
`OpenVolumetricNative/integrations/unity`; Unreal-facing code lives in
`Unreal/Plugins/OpenVolumetric` and links the same core.

## C++ namespaces

Namespaces follow ownership boundaries rather than individual source
subdirectories:

- `openvolumetric` contains every engine-independent runtime type from
  `OpenVolumetricCore`, including container, decoding, geometry, media, and support
  classes;
- `openvolumetric::authoring` contains reusable authoring and packaging types;
- `openvolumetric::unity` contains Unity-specific coordinators and graphics
  upload backends;
- exported `extern "C"` entry points remain in the global namespace so the
  `openvolumetric_*` Unity plug-in ABI is directly consumable by C#.

The current Unreal adapter is implemented with Unreal-facing `F` and `U`
types inside the Unreal plug-in module. It consumes the public
`openvolumetric::OpenVolumetricPlayer` façade and does not place Unreal types
or headers in the native core.

## Current core interfaces

| Interface | Responsibility |
| --- | --- |
| `AdaptivePolicy` | Converts representation metadata and runtime observations into deterministic stay, adjacent-switch, or retry-later decisions. |
| `AdaptivePlayerCoordinator` | Retains active playback while a generation-safe candidate session opens, seeks, primes, and commits atomically at an aligned boundary. |
| `OpenVolumetricPlayer` | Owns media and geometry decoding, seeking, timestamp matching, and complete CPU-side presentations. |
| `IAVDecoder` | Opens the combined MP4 and supplies timestamped YUV video, PCM audio, and compressed geometry. |
| `IGeometryDecoder` | Converts generation-tagged Draco payloads into timestamped engine-neutral `Mesh` values. |
| `Mesh` | CPU-side indices, positions, normals, and UVs with no engine dependency. |

The engine-neutral `OpenVolumetricPlayer` façade owns the concrete FFmpeg and
Draco implementation. Engine integrations use this façade rather than calling
FFmpeg or Draco APIs themselves. Unity adds a thin `UnityOpenVolumetricPlayer`
upload adapter behind the exported C ABI; the adapter contains no independent
decode or synchronization policy.

Unity's `ITexture` and `IMeshBuffer` contracts are integration details under
`integrations/unity/src/rendering/common`. They describe Unity render-device
resources and callbacks and are deliberately not public core interfaces.
Unreal translates the same CPU presentation into Unreal-owned resources
without depending on those Unity contracts.

Production construction remains intentionally simple: the default
`OpenVolumetricPlayer` selects the FFmpeg media and Draco geometry decoders.
An internal constructor accepts owned `IAVDecoder` and `IGeometryDecoder`
implementations so tests can provide deterministic substitutes. Containers
similarly accept an owned `IByteSource`. These narrow seams do not expose
concrete FFmpeg, Draco, MP4, local-file, or HTTP classes to an engine module.

## Lifecycle

An adapter follows this order:

1. Create one `AdaptivePlayerCoordinator` per engine player.
2. Resolve a direct MP4 or select the initial representation from a manifest.
3. Call `open()` and read `OpenVolumetricMediaInfo`.
4. Create engine texture, mesh, and audio resources.
5. Call `start()` to launch the core media and geometry workers.
6. Poll owned `OpenVolumetricPresentation` values from one monotonic engine
   playback clock and pull PCM through `read_audio()`.
7. Feed byte-source and switch observations to `AdaptivePolicy`, then pass any
   returned switch request to `AdaptivePlayerCoordinator`; do not reproduce
   thresholds, timers, or retry state in the engine component.
8. On shutdown, call `close()` before releasing engine graphics/audio objects;
   it cancels input, joins preparation and decode workers, and is idempotent.

Partially initialized instances must remain destructible. Engine teardown must
not release graphics resources while an engine render command can still refer
to them.

## Thread contract

| Thread | Permitted work |
| --- | --- |
| Engine/game thread | Playback state, presentation-clock updates, and synchronous seek requests. |
| Core demux thread | The only caller of container read/seek and the only mutator of FFmpeg container and codec state during playback. |
| Core geometry worker | Draco decode and publication of completed CPU meshes. |
| Engine render thread | Unity uploads the complete presentation selected by its native façade; do not retain engine graphics handles past teardown. |
| Engine audio thread | Call the coordinator's `read_audio()` to pull interleaved floating-point PCM; never initiate rendering or seeking. |
| Adaptive preparation worker | Open, seek, start, and prime one candidate player; publish only when its generation is current. |
| HTTP byte-source worker | Fetch demanded ranges and bounded fragment read-ahead without mutating decoder state. |

An adapter must not invoke FFmpeg or manipulate core queues from an engine
thread. Runtime seeks are submitted through `AdaptivePlayerCoordinator::seek()`
and executed synchronously by the active player between packet reads.

## Presentation contract

The engine supplies seconds from the same monotonic clock used to schedule
audio:

1. Poll the coordinator with the current engine clock.
2. The player moves compressed geometry up to its look-ahead limit.
3. The player selects video by PTS using half a source-frame interval plus
   0.1 ms.
4. It matches decoded geometry against the selected video PTS using the same
   tolerance.
5. It copies YUV planes and the mesh into one owned
   `OpenVolumetricPresentation` only when both are ready.
6. The adapter uploads texture and mesh from that same presentation.
7. Keep the previous complete presentation visible while either side returns
   `NotReady`.
8. Drop and diagnose a sample that returns `Missing`; never combine texture
   and geometry from different timestamp windows.

Frame numbers are diagnostic metadata and must not be used as the primary
synchronization key.

## Buffer ownership

| Data | Owner and validity |
| --- | --- |
| `OpenVolumetricPresentation` Y/U/V vectors and `Mesh` | Owned by the caller after `presentation()` returns `Ready`; valid independently of decoder queues and safe to translate immediately into engine resources. |
| Output passed to coordinator `read_audio()` | Owned by the engine caller. The core fills available samples and writes silence for an underrun. |
| Compressed geometry payload | Owned while moving between core queues; an engine adapter never retains or modifies it. |
| Unity graphics handles passed to its `ITexture`/`IMeshBuffer` adapters | Owned by Unity. The platform uploader may use them only under Unity's render-thread rules. |

The lower-level decoder interfaces still expose borrowed video pointers
internally, but engine integrations consume copied façade presentations and
must not bypass `OpenVolumetricPlayer` to retain those pointers.

## Seeking and looping

- The engine clock chooses the target time and loop boundary.
- Coordinator/player seek pauses packet production on the demux thread, seeks the
  MP4, flushes codecs, clears staged media/geometry, resets audio, and advances
  the playback generation.
- The player resets geometry decoding to that generation and decodes forward
  from the preceding access point until matching video and geometry are ready.
- Audio decoded before the requested target is discarded during preroll.
- Do not implement a second automatic rewind in an engine adapter or another
  worker thread.

## Errors and diagnostics

The Unity ABI returns a stable `OpenVolumetricResult` for every fallible
runtime operation. Categories distinguish argument/handle errors, unsupported
format, corruption, network failure, timeout, cancellation, decoder failure,
not-ready state, and internal failure. `openvolumetric_player_get_error`
copies the detailed UTF-8 message into a caller-owned buffer so logging does
not depend on a borrowed or thread-local pointer. The complete ABI contract is
documented in [NATIVE_API.md](NATIVE_API.md).

Core opening failures remain available through `IAVDecoder::get_last_error()`.
Engine adapters should attach this message to their normal logging and error UI.
`SYNC` diagnostics currently report dropped, duplicated, late, or mismatched
samples. An adapter must treat malformed input as a controlled playback
failure rather than continuing with stale pointers.

Structured buffer, recovery, fragment, adaptive-switch, audio-underrun, and
transfer-throughput counters are exposed through the façade. Engine
integrations must not parse log text as a data interface.

## Unreal implementation

The implemented Unreal Engine 5.8 plug-in contains:

- `OpenVolumetricRuntime`, a runtime module linked to
  `OpenVolumetricCore`;
- `FOpenVolumetricPlayerAdapter`, a private translation layer between Unreal
  containers/types and `openvolumetric::OpenVolumetricPlayer`;
- `UOpenVolumetricComponent`, a Blueprint-facing component exposing source
  selection, open, play, pause, seek, loop, status, and error state;
- `UDynamicMeshComponent` output with coordinate, normal-overlay, and
  UV-overlay conversion;
- a transient `UTexture2D` and two-sided unlit dynamic material;
- `USoundWaveProcedural` and `UAudioComponent` output for decoded PCM;
- `OpenVolumetricAuthoring`, an Editor-only module exposing the same OBJ,
  image, audio, preset, packaging, and verification workflow as Unity; and
- `/Game/OpenVolumetricSample`, a minimal sample level.

The current macOS implementation polls one complete timestamp-matched
presentation on the Unreal game thread, converts planar YUV420 data to BGRA,
updates the dynamic mesh, and enqueues a texture-region upload through three
reusable storage slots.
Audio is drained into a procedural sound wave with approximately 250 ms of
queued PCM. This is intentionally an initial engine adapter rather than a
final RHI-optimized path: a future implementation can move YUV conversion and
mesh/texture transfer onto render/RHI resources without changing the
container or core player contract.

Detailed backend resource ownership, valid calling threads, synchronization,
failure handling, and teardown order are documented in
[RENDERING.md](RENDERING.md).

Editor playback on macOS is implemented and manually validated. Packaged
build staging, Windows and additional RHI implementations, lifecycle stress
tests, and Unreal Android/Quest support remain outstanding.

## Public-boundary improvement backlog

These changes can further improve the adapter API without changing the file
format:

- replace `void*` graphics handles with adapter-owned upload callbacks;
- expose additional structured synchronization, queue, and drop counters;
- make lifecycle/state queries thread-safe and strongly typed.
