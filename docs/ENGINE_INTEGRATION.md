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
| `IVolumetricVideo` | Coordinates presentation time, media, geometry, seeking, and engine upload backends. |
| `IAVDecoder` | Opens the combined MP4 and supplies timestamped YUV video, PCM audio, and compressed geometry. |
| `IGeometryDecoder` | Converts generation-tagged Draco payloads into timestamped engine-neutral `Mesh` values. |
| `ITexture` | Engine/platform implementation that owns Y, U, and V texture resources and uploads one selected frame. |
| `IMeshBuffer` | Engine/platform implementation that uploads one engine-neutral mesh. |
| `Mesh` | CPU-side indices, positions, normals, and UVs with no engine dependency. |

The engine-neutral `OpenVolumetricPlayer` façade owns the concrete FFmpeg and
Draco implementation. Engine integrations use this façade rather than calling
FFmpeg or Draco APIs themselves. The older Unity upload coordinator still
implements its native graphics bridge behind the exported C ABI.

## Lifecycle

An adapter follows this order:

1. Create one coordinator instance for one volumetric player.
2. Create its media decoder, geometry decoder, texture uploader, and mesh
   uploader.
3. Open the combined MP4 through `IAVDecoder::init()`.
4. Initialize the geometry decoder.
5. Read `VideoInfo` and `AudioInfo`, then create engine texture, mesh, and
   audio resources.
6. Start the media and geometry workers.
7. Submit presentation time from one monotonic engine playback clock.
8. On shutdown, stop and join both workers before destroying queues, codec
   state, graphics resources, or the coordinator.

Partially initialized instances must remain destructible. Engine teardown must
not release graphics resources while an engine render command can still refer
to them.

## Thread contract

| Thread | Permitted work |
| --- | --- |
| Engine/game thread | Playback state, presentation-clock updates, and synchronous seek requests. |
| Core demux thread | The only caller of container read/seek and the only mutator of FFmpeg container and codec state during playback. |
| Core geometry worker | Draco decode and publication of completed CPU meshes. |
| Engine render thread | Select a complete presentation, upload YUV planes and mesh data, then release consumed frames. |
| Engine audio thread | Call `IAVDecoder::read_audio()` to pull interleaved floating-point PCM; never block on rendering or seeking. |

An adapter must not invoke FFmpeg, manipulate core queues, or mutate decoder
state from an engine thread. Runtime seeks are submitted through
`IAVDecoder::seek()` and executed synchronously by the demux thread between
packet reads.

## Presentation contract

The engine supplies seconds from the same monotonic clock used to schedule
audio:

1. Call `set_presentation_time()` on the coordinator.
2. Move compressed geometry up to the look-ahead limit into the Draco worker.
3. Select video by PTS using half a source-frame interval plus 0.1 ms.
4. Match decoded geometry against the selected video PTS using the same
   tolerance.
5. Upload texture and mesh only when both return `FrameMatchResult::Ready`.
6. Keep the previous complete presentation visible while either side returns
   `NotReady`.
7. Drop and diagnose a sample that returns `Missing`; never combine texture
   and geometry from different timestamp windows.
8. After upload, call `clean_frame_data()` and `clear_frame_data()` exactly
   once to release the selected video and mesh.

Frame numbers are diagnostic metadata and must not be used as the primary
synchronization key.

## Buffer ownership

| Data | Owner and validity |
| --- | --- |
| Y, U, V pointers returned by `get_video_data()` | Owned by the media decoder. Valid only until the selected frame is cleaned, a seek/reset completes, or the decoder is destroyed. Upload immediately; do not retain them in engine objects. |
| `Mesh` returned by `get_mesh_data()` | CPU value copied from the geometry queue. The adapter may use it during the current upload, but should not retain unbounded historical meshes. |
| Output passed to `read_audio()` | Owned by the engine caller. The core fills available samples and writes silence for an underrun. |
| Compressed geometry payload | Owned while moving between core queues; an engine adapter never retains or modifies it. |
| Graphics handles passed to `ITexture`/`IMeshBuffer` | Owned by the engine. The platform uploader may use them only under the engine's render-thread rules. |

The present pointer-returning video API requires the adapter to ensure that a
render upload cannot overlap destruction. A future façade should prefer an
explicit presentation lease or copied/upload callback, but adapters must
honour the lifetime above until that change is made.

## Seeking and looping

- The engine clock chooses the target time and loop boundary.
- `IAVDecoder::seek()` pauses packet production on the demux thread, seeks the
  MP4, flushes codecs, clears staged media/geometry, resets audio, and advances
  the playback generation.
- The adapter resets `IGeometryDecoder` to that generation.
- For a running player, call `prepare_presentation()` so decode advances from
  the preceding video keyframe until matching video and geometry are ready.
- Audio decoded before the requested target is discarded during preroll.
- Do not implement a second automatic rewind in an engine adapter or another
  worker thread.

## Errors and diagnostics

Opening failures are available through `IAVDecoder::get_last_error()`. Engine
adapters should attach this message to their normal logging and error UI.
`SYNC` diagnostics currently report dropped, duplicated, late, or mismatched
samples. An adapter must treat malformed input as a controlled playback
failure rather than continuing with stale pointers.

Structured counters are a future API improvement. Engine integrations should
not parse log text as a data interface.

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
updates the dynamic mesh, and enqueues a transient texture-region upload.
Audio is drained into a procedural sound wave with approximately 250 ms of
queued PCM. This is intentionally an initial engine adapter rather than a
final RHI-optimized path: a future implementation can move YUV conversion and
mesh/texture transfer onto render/RHI resources without changing the
container or core player contract.

Editor playback on macOS is implemented and manually validated. Packaged
build staging, Windows and additional RHI implementations, lifecycle stress
tests, and Unreal Android/Quest support remain outstanding.

## Public-boundary improvement backlog

These changes can improve the adapter API without changing the file format:

- replace raw owned members with factories and RAII smart pointers;
- expose a single engine-neutral player façade instead of concrete decoder
  construction;
- replace `void*` graphics handles with adapter-owned upload callbacks;
- introduce a presentation lease that makes video-plane lifetime explicit;
- expose structured synchronization, queue, and drop counters;
- make lifecycle/state queries thread-safe and strongly typed.

Until those improvements are implemented, the rules in this document are the
required integration contract.
