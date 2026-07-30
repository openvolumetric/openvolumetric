# OpenVolumetric: Technical Overview

## 1. System purpose

OpenVolumetric is an open-source volumetric-video authoring and playback
system. Its principal objective is to represent
appearance, time-varying geometry, and audio in a single conventional media
container and to decode that representation through an engine-independent
native core.

The current implementation consists of:

- A C++17 runtime library, `OpenVolumetricCore`.
- An Editor-only C++ authoring library, `OpenVolumetricAuthoringCore`.
- A Unity native plug-in and managed C# integration.
- An Unreal Engine 5.8 runtime and Editor plug-in.
- Graphics upload backends for D3D11, Metal, and Vulkan.
- Unity and Unreal Editor authoring interfaces accepting numbered image and
  OBJ sequences.
- A custom MP4 geometry track containing complete Draco topology keyframes
  and optional Draco position-only updates.

The Unity integration has been exercised on macOS using Metal and on Meta
Quest using Android/Vulkan. The Unreal integration has been exercised in
Unreal Editor 5.8 on macOS with synchronized geometry, texture, and audio. A
Windows D3D11 implementation exists but still requires clean-build validation
on the current renamed repository. Unreal packaged builds and a Nuke
integration remain planned.

The main source-level boundaries are documented in `OpenVolumetricNative/src/core`
and `docs/ENGINE_INTEGRATION.md`.

## 2. Architectural overview

```text
Authoring
=========
numbered images --> FFmpeg --> HEVC/H.264 texture video --+
optional audio ---> FFmpeg --> AAC audio -----------------+
numbered OBJ -----> canonical topology classification ----+
                         | complete mesh / position update |
                         +-------------> Draco ------------+
                                                          |
                                                          v
                                              OpenVolumetric MP4 packer
                                                          |
                                               verification pass
                                                          |
                                                          v
                                                 OpenVolumetric MP4 file


Runtime
=======
OpenVolumetric MP4
    |
    v
FFmpegMp4VolumetricContainer
    |
    | one demux order
    v
AVDecoderFFMPEG
    |                    |                         |
    | video packets      | audio packets           | vvge samples
    v                    v                         v
FFmpeg video decoder   FFmpeg audio decoder   compressed geometry queue
    |                    |                         |
Y/U/V frame queue      float PCM ring              v
                                              Draco worker thread
                                                   |
                                             decoded mesh queue
    |                                              |
    +---------------------- PTS matching ----------+
                            |
                     complete presentation
                            |
                  engine-specific host adapter
              Unity native upload / Unreal assets
                            |
                  engine texture, mesh, and audio
```

### 2.1 Layer separation

The source tree separates the system into four primary layers:

| Layer | Responsibility |
| --- | --- |
| `OpenVolumetricCore` | Container access, FFmpeg decoding, Draco decoding, buffering, timestamps, synchronization, seeking, and engine-neutral mesh data |
| `OpenVolumetricAuthoringCore` | Shared presets and validation, FFmpeg argument construction, Draco encoding, MP4 packaging, and output verification |
| `integrations/unity` | Thin `UnityOpenVolumetricPlayer` adapter, thread-safe C ABI registry, graphics API access, and native GPU uploads |
| `Unreal/Plugins/OpenVolumetric` | Unreal runtime component, core adapter, dynamic mesh/texture/audio output, and Editor authoring interface |

The core deliberately contains no Unity or Unreal headers or types. Graphics
operations are represented through engine-neutral presentations and, for the
Unity native path, abstract interfaces such as `ITexture` and `IMeshBuffer`.
`OpenVolumetricPlayer` owns media/geometry decoding, seek state, timestamp
matching, and complete CPU-side presentations. An engine adapter supplies host
resources and invokes this single common player.

The implemented Unreal plug-in demonstrates that this boundary supports a
second engine without duplicating container, codec, synchronization, or seek
logic. Nuke or other hosts can follow the same separation.

## 3. Container and file format

### 3.1 MP4 organization

An OpenVolumetric file is an ISO Base Media File Format/MP4 container with the
following logical tracks:

```text
OpenVolumetric.mp4
|-- Video: HEVC or H.264 texture imagery
|-- Audio: AAC or another FFmpeg-supported MP4 audio stream (optional)
`-- Data: timed Draco geometry samples, identified by `vvge`
```

The runtime requires:

- Exactly one video stream.
- Exactly one data stream tagged `vvge`.
- Zero or one audio stream.

Missing, duplicate, or malformed required streams cause initialization to
fail.

Ordinary MP4 players generally ignore the unknown geometry data track and
continue to play the video and audio. This permits an OpenVolumetric file to retain a
useful conventional preview.

### 3.2 Geometry sample entry

The geometry stream uses the project-specific four-character sample entry:

```text
vvge
```

FFmpeg 8.1.2 does not directly author an arbitrary binary MP4 data stream with
this tag. The packer therefore creates a generic binary data stream identified
as `gpmd` and subsequently performs an equal-length replacement of the
corresponding declaration within the MP4 `moov` structure.

This does not change sample offsets or box sizes, but it is provisional. It
depends on the generated MP4 structure and has not yet been standardized or
tested against a broad set of MP4 implementations.

The final packaging pass enables FFmpeg's `faststart` MP4 mode. FFmpeg
relocates `moov` ahead of `mdat` when the trailer is written, after which the
authoring verifier explicitly checks the top-level box order. Outputs whose
metadata still follows their media payload are rejected rather than
published.

### 3.3 Geometry packet format

Each geometry sample contains the current 40-byte, big-endian `VVGF` version
2 header followed by a Draco payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 bytes | Magic: `VVGF` |
| 4 | 2 bytes | Version (`2`) |
| 6 | 2 bytes | Header size (`40`) |
| 8 | 1 byte | Coding mode |
| 9 | 1 byte | Payload codec |
| 10 | 2 bytes | Flags |
| 12 | 4 bytes | Source frame number |
| 16 | 8 bytes | Topology identifier |
| 24 | 4 bytes | Referenced keyframe frame number |
| 28 | 4 bytes | Vertex count |
| 32 | 4 bytes | Triangle count |
| 36 | 4 bytes | Payload size |
| 40 | variable | Draco mesh or point-cloud payload |

`IndependentMesh` packets contain a complete Draco mesh and establish a
random-access topology keyframe. `PositionUpdate` packets contain an
order-preserving Draco point cloud with absolute positions and reference the
active preceding topology keyframe. They are not deltas from the preceding
frame, so losing one update does not prevent decoding a later update.

The MP4 sample timestamp, not the source frame number, is authoritative for
synchronization. The frame number exists for validation and diagnostics.

Multi-byte fields are explicitly serialized in big-endian/network order. The
complete normative description is in
[GEOMETRY_PACKET.md](GEOMETRY_PACKET.md).

### 3.4 Timing model

The packer derives one geometry timestamp and duration from each encoded video
sample. It does not reconstruct timing from filenames or assume that the
encoded video is strictly constant-frame-rate.

This preserves:

- Non-zero video start times.
- Encoder-generated sample timing.
- Variable sample durations.
- Direct correspondence between video and geometry presentations.

Geometry packets use their video sample's presentation timestamp for both PTS
and DTS. Position updates depend on a topology keyframe but do not require
sample reordering.

## 4. Authoring and encoding pipeline

The authoring workflow is exposed through both engines:

```text
Unity:  Tools > OpenVolumetric > Encoder
Unreal: Tools > OpenVolumetric Encoder
```

### 4.1 Inputs

The authoring interface accepts:

- A contiguous, numerically named image sequence.
- A matching sequence of OBJ meshes.
- An optional audio file.
- A target MP4 path.
- A frame rate and encoding preset.

Image and OBJ frame numbers must match exactly. Numbering gaps, inconsistent
padding, empty inputs, and inconsistent extensions are rejected before
encoding begins.

### 4.2 Geometry encoding

Every OBJ file is parsed and encoded through the Draco library linked into
`OpenVolumetricAuthoring`. No standalone `draco_encoder` executable is required.

The author can control:

- Position quantization.
- Normal quantization.
- Texture-coordinate quantization.
- Draco encoding speed.
- Draco decoding speed.
- Whether shared topology is reused.
- An optional maximum number of geometry frames between complete Draco
  reference meshes.

The temporary output is a numbered `.drc` sequence. Those files are an
intermediate authoring representation and are not accepted by the runtime
player.

With geometry compression enabled, the packer canonicalizes each OBJ,
fingerprints indices, winding, UV mapping, and attribute layout, and segments
consecutive frames with matching topology. The first sample in a reusable
window is an order-preserving complete Draco mesh; later samples are
order-preserving Draco point clouds containing positions only. Topology
changes fall back automatically to a complete mesh.

The optional geometry keyframe limit bounds a window even when topology
remains unchanged. A value of `N` permits one complete reference mesh plus at
most `N - 1` position updates. Disabling the limit permits reuse until the
topology changes. Singleton/independent meshes use Draco's normal mesh method
for lower size and decode cost.

### 4.3 Texture and audio encoding

The engine Editor window invokes an external FFmpeg executable to:

1. Read the numbered image sequence.
2. Encode it as HEVC using `libx265` or H.264 using `libx264`.
3. Convert imagery to planar 8-bit `yuv420p`.
4. Encode optional audio as 192 kbit/s AAC.
5. Pad short audio where necessary and constrain the output to the relevant
   media duration.

FFmpeg remains an external authoring executable because its command-line
encoders provide mature image-sequence and audio handling. MP4 packaging and
verification, by contrast, are performed in-process through the OpenVolumetric
authoring library.

### 4.4 Platform presets

Both authoring interfaces presently provide:

- **Desktop Local:** HEVC CRF 20, three reference frames, and balanced Draco
  speed without a network-rate ceiling.
- **Desktop Streaming:** HEVC CRF 23 constrained to 16 Mbps with a 32 Mbps
  encoder buffer, 60-frame video GOPs, and 60-frame geometry reference
  windows.
- **Quest Local:** H.264 CRF 23, no B-frames, one reference frame, reduced
  geometry precision, and maximum Draco decode speed.
- **Quest Streaming:** HEVC CRF 27 constrained to 8 Mbps with a 16 Mbps
  encoder buffer, one-second video GOPs, one-second full-geometry reference
  windows, and maximum Draco decode speed.
- **Custom:** explicit video and geometry controls.

These presets trade compression efficiency, precision, decoder complexity,
and network-rate consistency. They do not resize the input imagery. The
streaming ceilings cover the texture-video track rather than the geometry
and audio streams, so the final container rate is higher.

Preset values, numbered-source validation, and FFmpeg argument construction
are implemented once in `AuthoringWorkflow`. Unity consumes them through the
authoring C ABI, while the Unreal Editor module links the same C++ authoring
core. The engine windows retain only host-specific file pickers, progress
reporting, process launching, and UI state.

### 4.5 Packaging

Once the media MP4 and Draco sequence exist, `OpenVolumetricAuthoringCore`:

1. Opens the media MP4 using FFmpeg.
2. Collects the actual video sample PTS and duration values.
3. Verifies that video and geometry sample counts agree.
4. Copies existing media packets without re-encoding.
5. Creates a binary data stream.
6. Wraps each Draco payload in a `VVGF` packet.
7. Interleaves geometry using the corresponding video timing.
8. Finalizes the MP4.
9. Replaces the generic data sample entry with `vvge`.
10. Reopens and verifies the completed result.

The selected destination is replaced only after verification succeeds.
Temporary files are then removed.

### 4.6 Verification

Verification checks:

- Geometry sample count.
- Every serialized geometry packet against the packet prepared by the packer.
- Geometry PTS and duration against the corresponding video sample.
- Stream identity.
- A representative seek near the middle of the sequence.

This transactional process is an important reliability feature: an existing
output is not destroyed until the candidate output passes round-trip
verification.

## 5. Decoding pipeline

### 5.1 Container abstraction

`IVolumetricContainer` defines an engine-independent packet interface. Its
current implementation, `FFmpegMp4VolumetricContainer`, exclusively owns the
`AVFormatContext`.

Container bytes are supplied through the engine-independent `IByteSource`
contract. Current path-based playback creates a seekable
`LocalFileByteSource`; FFmpeg reads and seeks it through a custom
`AVIOContext`. HTTP and HTTPS inputs use `HttpRangeByteSource`, whose dedicated
libcurl worker fills an 8 MiB LRU cache in 256 KiB ranges. FFmpeg remains on
the demux thread and waits for cache blocks rather than performing socket I/O.
The source contract provides terminal cross-thread cancellation so player
destruction can interrupt an outstanding request before joining the demux
worker. No transport code enters Unity or Unreal.

Packets crossing this interface contain:

- Stream kind.
- Stream index.
- PTS, DTS, and duration.
- Timestamp validity flags.
- Stream time base.
- An owned copy of the compressed payload.

Owning packet bytes allows samples to cross component and thread boundaries
without retaining FFmpeg packet storage.

### 5.2 Unified demultiplexing

A single `AVDecoderFFMPEG` worker is the sole caller of `av_read_frame()`. It
routes packets to video, audio, or geometry processing while preserving their
original interleaving.

This avoids multiple readers competing for a shared demuxer and establishes a
single serialization point for seeking and codec flushing.

Each stream has separate pending packet storage:

- Video: up to 64 pending compressed packets.
- Audio: up to 64 pending compressed packets.
- Geometry: up to 128 pending compressed packets.

When a decoded output queue is full, the associated compressed packets are
staged. If both the output and pending staging are full, demuxing retains the
current packet and pauses until capacity becomes available.

Audio staging is drained first to reduce the chance that geometry or video
backpressure starves the audio callback.

### 5.3 Video decoding

FFmpeg performs software video decoding. The video codec context is configured
with up to eight decoder threads.

Decoded frames are retained as `AVFrame` objects in a bounded queue of 32
frames. Their `best_effort_timestamp` values are converted to seconds using
the video stream time base.

The output format expected by the graphics integrations is planar YUV420P:

- Full-resolution Y plane.
- Half-width, half-height U plane.
- Half-width, half-height V plane.

Frames older than the current presentation window are released. If the next
available frame is later than the permissible matching window, the result is
marked as missing rather than presenting a temporally incorrect frame.

### 5.4 Audio decoding

If audio is present, FFmpeg decodes it and `libswresample` converts it to:

- Interleaved floating-point PCM.
- Stereo output.
- The source stream's sample rate.

PCM is stored in a four-second single-producer/single-consumer ring buffer.
Atomic monotonic read and write positions allow the FFmpeg worker to produce
samples while Unity's audio thread consumes them without a mutex.

If the callback requests more data than is available, the remainder is filled
with silence. The decoder records these partial reads as underruns.

The ring also publishes an engine-neutral audio timeline snapshot: the media
timestamp represented by its read head, the duration of queued PCM, and the
underrun count. A timeline origin pairs a monotonic ring position with the
media timestamp established at open or seek. This permits integrations to
make readiness and recovery decisions without depending on FFmpeg structures
or inferring buffered time from wall-clock delays.

During seeking, buffered PCM is discarded by advancing the read position to
the current write position. The counters are deliberately not reset to zero
because an audio callback already in flight could otherwise publish an old
position and cause unsigned counter underflow.

### 5.5 Geometry decoding

Geometry samples are parsed to remove and validate their `VVGF` framing. The
remaining Draco payload and its PTS are placed in a bounded compressed queue.

A separate `GeometryDecoderDraco` worker handles both coding modes. A complete
mesh replaces the active topology cache. A position update must match the
cache's playback generation, topology ID, keyframe number, vertex count, and
triangle count; it then replaces positions, reuses indices and UVs, and
recalculates area-weighted normals. Both paths publish an engine-neutral mesh
containing:

- 32-bit triangle indices.
- Three-component positions.
- Three-component normals.
- Two-component texture coordinates.

Complete source meshes require position, normal, and texture-coordinate
attributes. Position-update payloads intentionally contain only positions.

Queue capacities are:

- 256 compressed Draco samples.
- 64 decoded meshes.
- 128 compressed geometry samples in the media decoder before transfer to the
  Draco worker.

`OpenVolumetricPlayer` submits geometry up to four seconds ahead of the
requested presentation time so meshes are normally decoded before their
matching texture frame is required.

### 5.6 Presentation matching

Visual synchronization is timestamp-based. The media decoder first selects a
video frame near the engine's requested presentation time. The geometry
decoder then selects a mesh relative to that video frame's actual PTS.

The tolerance is:

```text
epsilon = (0.5 / video_frame_rate) + 0.0001 seconds
```

A fallback of approximately 17 ms is used if the frame rate is unavailable.

A frame is uploaded only when both texture and geometry are ready within this
window. If either is still being decoded, the previously completed
presentation remains visible. This avoids rendering a new texture over an old
mesh or vice versa.

If a sample is provably unmatchable, for example if the next geometry PTS is
already beyond the selected video window, the obsolete sample is dropped.

### 5.7 Seeking and looping

Runtime seek requests are submitted by an engine thread but executed by the
demux-owner thread between packet reads. A seek performs the following
coordinated reset:

1. Seek the MP4 demuxer backward toward the requested time.
2. Flush the video and audio codecs.
3. Reset the audio resampler.
4. Clear decoded video frames.
5. Clear compressed geometry and pending packet queues.
6. Discard buffered audio.
7. Increment the playback generation.
8. Decode forward until a complete texture/geometry presentation is
   available.

Audio samples preceding the requested target are discarded during preroll.

Every seek or loop increments a generation counter carried with geometry work.
A Draco decode that began before the reset is rejected if it finishes in a
later generation. This prevents a stale mesh from appearing after looping or
seeking.

## 6. Runtime ownership and memory management

### 6.1 Ownership hierarchy

Each engine-neutral `OpenVolumetricPlayer` owns one combined-media decoder and
one geometry decoder. Unity's `UnityOpenVolumetricPlayer` additionally owns one
texture uploader and one mesh-buffer uploader plus their platform-specific
graphics state. Unreal instead owns its engine objects in
`UOpenVolumetricComponent` and its private player adapter.

More specifically:

- `FFmpegMp4VolumetricContainer` owns the `AVFormatContext`.
- `AVDecoderFFMPEG` owns codec contexts, the resampler, packet staging,
  decoded video frames, and the audio ring.
- `GeometryDecoderDraco` owns compressed and decoded geometry queues.
- Unity owns the visible `Mesh`, `Texture2D`, `Material`, `AudioSource`, and
  `AudioClip`.
- Unity also owns the destination mesh buffers.
- Texture ownership differs by graphics backend.

Worker threads must be stopped and joined before their queues or codec
resources are destroyed.

### 6.2 Queue storage

Video and geometry use a mutex-protected `BoundedQueue<T>`. The queue has
explicit states:

- Open.
- End of stream.
- Error.

Queue capacity is fixed at construction and pushes are non-blocking.
Backpressure is handled by the producing component rather than by waiting
inside the queue.

The audio path uses a fixed-size vector as a circular buffer and atomic
counters because blocking or mutex acquisition is undesirable in Unity's
audio callback.

### 6.3 Allocation behavior

The current implementation is bounded in queue length but is not
allocation-free:

- FFmpeg frames are allocated as decoded output is received.
- Compressed packet payloads are copied into owned vectors.
- Audio conversion currently allocates a temporary float vector for decoded
  audio frames.
- Draco creates a new decoded mesh for each geometry sample.
- Mesh vectors are resized according to each decoded frame.
- Some GPU backends use staging allocations or copies per presentation.

This behavior has been adequate for present workloads, but memory pools and
reusable frame storage would improve predictability on constrained platforms.

## 7. Threading model

A typical playback instance uses the following execution contexts:

| Thread/context | Responsibilities |
| --- | --- |
| Unity main thread | Component lifecycle, file preparation, resource creation, and playback commands |
| FFmpeg worker | MP4 reads, packet routing, video decoding, audio decoding, and geometry extraction |
| Draco worker | Geometry decompression |
| Unity render thread | Timestamp matching and GPU texture/mesh upload |
| Unity audio thread | Pulling PCM from the lock-free ring buffer |
| Unreal game thread | Component lifecycle, presentation polling, dynamic-mesh replacement, texture-update submission, audio queueing, and playback commands |
| Unreal render thread | Execution of transient texture-region updates submitted by the game thread |
| Unreal audio renderer | Consumption of PCM queued through `USoundWaveProcedural` |
| FFmpeg internal workers | Parallel video decoding, up to the configured thread count |

The shared demux and codec state is mutated only by the FFmpeg worker. Engine
calls do not directly flush codec state while decoding is active.

On D3D11, the current texture and mesh upload paths additionally create
short-lived CPU copy threads inside a render operation and join them before
returning. This is an implementation-specific optimization attempt, not part
of the core architecture, and may be less efficient than a persistent worker
or direct copies for small workloads.

The Unity native registry owns players with `std::unique_ptr` and is protected
by a `std::shared_mutex`. API calls and render events retain a shared lock for
the complete instance operation; creation and destruction take the exclusive
lock. Destruction therefore cannot invalidate a player while an outstanding
render callback or audio/API call is using it.

## 8. Engine integrations

### 8.1 Unity native boundary

Unity communicates with `AudioPluginOpenVolumetricUnity` through an exported
C ABI. Its CMake target remains `OpenVolumetricUnityPlugin`; the output name
uses Unity's required `AudioPlugin` prefix so the same library can expose the
runtime API and the native DSP effect.
Operations include:

- Instance creation and destruction.
- MP4 loading.
- Start, stop, and seek.
- Presentation-time updates.
- Video and audio metadata queries.
- PCM reads.
- Texture-handle exchange.
- Mesh-buffer registration.
- Error reporting.
- Render-event retrieval.

Instances receive integer identifiers used by both managed calls and Unity
render events. Each registry entry is a `UnityOpenVolumetricPlayer`, a thin
adapter around `OpenVolumetricPlayer` that publishes the requested engine
clock, uploads only complete matched presentations, and atomically publishes
the most recently presented timestamp. Metal, D3D11, and Vulkan therefore
share identical presentation and lifecycle policy rather than implementing
separate platform coordinators.

### 8.2 Unity managed component

`OpenVolumetricDecoder.cs` wraps the native API and
`OpenVolumetric.cs` exposes the main component. Together they create:

- A Unity mesh with preallocated vertex and index capacity.
- External or native texture objects for the three YUV planes.
- A silent carrier `AudioClip` that routes the native effect through Unity's
  DSP graph.
- Material bindings for Y, U, and V.
- A native render event for every required presentation.

The current managed mesh allocation supports up to:

- 65,535 vertices.
- 100,000 triangles.

These are configured limits rather than constraints inherent to Draco or MP4.

### 8.3 Rendering

Unity issues `GL.IssuePluginEvent` with the playback instance identifier.
Unity then calls the native render callback on its render thread.

Within one render event, the native backend:

1. Selects a video frame.
2. Finds the matching mesh.
3. Uploads all three YUV planes.
4. Updates the vertex and index buffers.
5. Marks the presentation PTS as completed.

Texture and geometry are therefore committed together at the integration
boundary.

A Unity shader samples the three planes and performs YUV-to-RGB conversion on
the GPU.

### 8.4 Playback clock

Before initial playback, Unity holds the presentation at timestamp zero until
both a complete texture/geometry presentation and sufficient decoded PCM are
ready. The PCM requirement is derived from Unity's configured DSP buffer
length and buffer count with a scheduling margin. Audio and visuals are then
released at one future DSP timestamp.

Unity supplies decoded PCM from a real-time-safe native spatializer callback
inside its DSP graph. The callback receives Unity's absolute DSP sample tick,
converts it to media time using the shared scheduled start, and reads PCM
directly from the native player. It performs no allocation, locking, or
logging. A fixed-capacity linear converter reconciles the encoded sample rate
with Unity's output rate; this is required for 44.1 kHz media on Quest's 48 kHz
mixer. Visual presentation advances from the same DSP timeline, so no
empirical output-latency correction is required.

The native DSP bridge currently supports one active OpenVolumetric player and
occupies Unity's project-wide spatializer plug-in slot. These are integration
constraints rather than core decoder limitations. A dedicated native
AudioMixer effect is the intended route to multiple players and coexistence
with another spatializer.

The managed player advances visual media time from monotonic DSP-time deltas.
The native PCM status supplies buffer readiness and underrun evidence; it does
not introduce a second independent playback clock. Seeks and loop resets are
performed for every native stream as one generation so queued PCM and visual
presentations cannot intentionally cross a timeline boundary.

If rendering is suspended long enough for a large forward DSP discontinuity,
as can occur when a desktop window is minimized, Unity audio may continue
while the managed visual update stops. On resume, the integration reads the
carrier clip's media position and performs a unified seek. This flushes
audio, texture, and geometry queues and restarts them in one new generation
instead of allowing a permanent offset.

Looping is controlled by the managed clock. At the loop boundary, Unity
requests a unified native seek rather than independently resetting the
streams.

The player includes guarded recovery for sustained decoder lag. Brief lateness
retains the last complete presentation; a longer failure to advance can
trigger a coordinated seek near the current playback position.

### 8.5 Android asset handling

Desktop `StreamingAssets` entries are accessible as ordinary files. On
Android, assets reside inside the application package and cannot be passed
directly to FFmpeg as filesystem paths.

The Unity layer therefore copies the selected MP4 to a readable persistent
cache location before opening it through the native plug-in. Consequently,
current Android playback is local-file based even when content was originally
packaged as an application asset.

### 8.6 Unreal Engine integration

The Unreal Engine 5.8 plug-in has separate runtime and Editor modules:

- `OpenVolumetricRuntime` links the engine-neutral core and exposes
  `UOpenVolumetricComponent` to C++ and Blueprints.
- `FOpenVolumetricPlayerAdapter` privately converts between the core's STL
  values and Unreal containers, meshes, pixels, and strings.
- `OpenVolumetricAuthoring` exposes the encoder from Unreal's Tools menu and
  links the reusable authoring core.

`UOpenVolumetricComponent` owns one core player. On each playing game-thread
tick it advances the presentation time, drains audio to prevent demux
backpressure, and requests a complete timestamp-matched presentation. Ready
geometry is converted from OpenVolumetric metres/Y-up coordinates into Unreal
centimetres/Z-up coordinates and installed in a `UDynamicMeshComponent`.
Normals and UVs are written through dynamic-mesh overlays.

The initial texture path converts the core's planar YUV420 frame to BGRA on
the CPU, then submits an asynchronous `UTexture2D::UpdateTextureRegions`
upload. A dynamic instance of the supplied two-sided unlit material binds the
transient texture. The component disables shadow casting, emissive-light
source behavior, and dynamic indirect-light contribution so captured colour
is not relit or injected into Lumen.

Decoded float PCM is converted to signed 16-bit samples and queued through
`USoundWaveProcedural`; the component maintains approximately 250 ms of audio
to absorb game-thread jitter. Open, play, pause, seek, close, looping, status,
duration, current time, and error state are exposed through the component.

The sample project includes `/Game/OpenVolumetricSample`. Basic synchronized
geometry, texture, and audio playback is manually validated in Unreal Editor
5.8 on macOS. The current implementation is intentionally portable and
functional rather than RHI-optimal; GPU YUV conversion, direct render-thread
mesh uploads, packaged dependency staging, and additional platforms remain
future work.

## 9. Graphics backends

### 9.1 D3D11

The Windows backend:

- Creates dynamic single-channel D3D11 textures.
- Exposes shader-resource views to Unity.
- Updates textures with `D3D11_MAP_WRITE_DISCARD`.
- Maps Unity-owned dynamic vertex and index buffers.
- Copies decoded mesh data into those buffers.

D3D11 driver-managed resource renaming helps prevent overwriting data still in
GPU use.

### 9.2 Metal

The macOS backend uses Unity's Metal interface and native Metal resources. It
uploads planar textures and dynamic mesh data during Unity's render callback.

The current implementation has historically used per-frame staging storage. A
reusable in-flight upload ring has been identified as a future optimization,
provided command-buffer ordering prevents staging memory from being
overwritten while the GPU is consuming it.

### 9.3 Vulkan

The Android/Quest backend uses Unity's Vulkan plug-in interface. Unity creates
the external textures, then returns the native image handles to the plug-in
through a registration handshake.

Uploads use host-visible, coherent Vulkan staging buffers and Unity-managed
command-buffer integration. The mesh implementation maintains multiple upload
slots so CPU writes do not overwrite a transfer that is still in flight.

Vulkan is currently required for Quest; OpenGL ES is not implemented as a
fallback.

## 10. Supported and validated platforms

| Host/platform | Backend | Status |
| --- | --- | --- |
| Unity 6 on macOS | Metal | Implemented and manually validated |
| Unity on Windows x64 | D3D11 | Implemented; clean current-repository validation remains outstanding |
| Unity on Meta Quest/Android ARM64 | Vulkan | Implemented and validated on Quest 2 |
| Unity on Meta Quest 3S | Vulkan | Intended target; final physical-device profiling remains outstanding |
| Unreal Engine 5.8 Editor on macOS | Dynamic Mesh, transient BGRA texture, procedural audio | Implemented and manually validated |
| Unreal packaged applications | Initial macOS target | Dependency staging and packaged-build validation outstanding |
| Linux | Core only | Native core can build; no engine rendering integration |
| Nuke | Feasibility planned | Not implemented |

The Android preset targets:

- `arm64-v8a`.
- Android API level 29.
- Unity's installed Android NDK.
- Vulkan.

Authoring shared libraries are built for macOS and Windows but are excluded
from Android player builds.

## 11. Dependencies and build system

### 11.1 Build system

The native project uses:

- CMake 3.20 or newer.
- C++17.
- Ninja presets.
- vcpkg manifest mode.

The principal targets are:

| Target | Type | Purpose |
| --- | --- | --- |
| `OpenVolumetricCore` | Static library | Engine-neutral decoding |
| `OpenVolumetricAuthoringCore` | Static library | Encoding, packaging, and verification |
| `OpenVolumetricAuthoring` | Shared library | Unity Editor authoring C API |
| `OpenVolumetricUnityPlugin` | Shared library target | Unity runtime and native DSP integration; output as `AudioPluginOpenVolumetricUnity` |
| `OpenVolumetricRuntime` | Unreal module | Blueprint/C++ runtime component and core adapter |
| `OpenVolumetricAuthoring` | Unreal module | Unreal Editor authoring interface |

### 11.2 FFmpeg

The vcpkg manifest pins a baseline and requests FFmpeg with:

- `avcodec`.
- `avformat`.
- `swresample`.

FFmpeg is used for:

- MP4 demultiplexing.
- Video decoding.
- Audio decoding.
- Audio resampling.
- MP4 muxing and verification.

Runtime FFmpeg libraries are linked into the native plug-in build, so users do
not install a separate FFmpeg runtime.

The Unity and Unreal authoring windows require an FFmpeg executable containing
`libx264`, `libx265`, and AAC encoding support.

### 11.3 Draco

Draco is acquired through vcpkg and linked directly into both runtime and
authoring targets.

It provides:

- OBJ-to-Draco mesh encoding in the authoring library.
- Draco-to-mesh decoding in the runtime.
- Attribute quantization and encode/decode-speed controls.

No Draco submodule or standalone encoder executable is required.

### 11.4 HTTP transport

libcurl is acquired through vcpkg with TLS enabled and is linked into the
runtime core. The current portable build uses OpenSSL and zlib transitively.
libcurl performs metadata and bounded byte-range transfers only on the
`HttpRangeByteSource` worker; it does not replace FFmpeg's demuxing role.

## 12. Streaming model

OpenVolumetric's container structure is conceptually compatible with streaming
because video, audio, and geometry are timestamped and interleaved in one MP4.
The decoder also processes packets incrementally and uses bounded queues rather
than loading an entire sequence into memory.

The runtime now supports the first progressive-download layer. Its
engine-independent byte-source boundary accepts local seekable files and
HTTP/HTTPS resources with byte-range support. Network reads are performed by
a dedicated libcurl worker and retained in a bounded block cache. FFmpeg
continues to demux through a custom `AVIOContext`; it does not own the network
transport. Cancellation interrupts an active transfer before player teardown
joins the demux worker.

Newly authored conventional MP4 outputs are fast-start files, so track
metadata is available before media payloads. This permits progressive startup
and seeking through byte ranges without first caching the complete asset.
Servers must expose a stable content length and support range requests.

Unity's optional `videoUrl` and Unreal's optional `SourceUrl` pass HTTP(S)
locations to the same core `OpenVolumetricPlayer::open()` entry point used by
local paths. Each integration exposes a thread-safe snapshot containing the
resource size, bounded-cache occupancy, cumulative downloaded bytes, and
range-request count. URL query strings are not written to native diagnostic
logs.

The runtime does not yet provide:

- Predictive buffered-duration or rebuffer-state diagnostics.
- Fragmented MP4/CMAF authoring.
- Live geometry ingest.
- Adaptive bitrate selection.
- Representation switching.
- Network jitter buffers.
- Recovery from missing network segments.

The next streaming layer will add fragmented MP4 packaging and segment
scheduling above the implemented byte-source transport. Random access also
requires geometry samples or dependency windows aligned with fragment and
seek boundaries.

## 13. Performance characteristics

Current performance evidence is observational rather than a controlled
academic evaluation.

### 13.1 Desktop

Previous macOS testing reported very low Unity-side CPU time and an effectively
uncapped scene frame rate. These figures suggest that playback is not currently
the dominant workload in the test scene, but values such as 2,000 frames/s
should not be treated as decoder throughput because Unity may render the same
encoded presentation many times between media frames.

A paper evaluation should separately measure:

- Demux time.
- Video decode time.
- Draco decode time.
- Texture upload time.
- Mesh upload time.
- Render-thread cost.
- Memory high-water mark.
- Dropped or duplicated presentations.
- Seek latency.

### 13.2 Quest

Quest 2 testing maintained the headset refresh target of approximately 72
frames/s for more than ten minutes with synchronized texture, geometry, and
audio. Seeking and looping were exercised successfully, and no concerning
thermal behavior was observed during that test.

Performance-oriented authoring presets materially improved playback stability
by reducing video prediction complexity and selecting faster Draco decoding.

Nevertheless:

- The video path still uses software decoding.
- Quest 3S has not received the same sustained profiling.
- Battery consumption has not been quantified.
- Thermal measurements were qualitative.
- Results currently cover a small number of sample sequences.

The roadmap allows introduction of an Android MediaCodec backend if software
HEVC decoding proves insufficient.

### 13.3 Latency versus buffering

The present queue sizes and four-second geometry lookahead favor uninterrupted
local playback over minimal latency. This is appropriate for prerecorded
content but would need adjustment for interactive or live streaming.

## 14. Extensibility

### 14.1 Engine integrations

The core exposes engine-neutral media, geometry, timing, and upload
interfaces. A new engine integration is expected to provide:

- Lifecycle wrapping.
- A presentation clock.
- Texture allocation or registration.
- Dynamic mesh-buffer upload.
- Procedural audio delivery.
- Render-thread scheduling.
- Packaging rules for the native dependencies.

The implemented Unreal adapter already uses the same façade for dynamic mesh,
transient texture, and procedural audio output without putting Unreal types
into `OpenVolumetricCore`. Its initial CPU YUV conversion can be replaced by
an RHI-specific path behind the same boundary.

### 14.2 Alternative decoders

`IAVDecoder` and `IGeometryDecoder` provide abstraction boundaries for:

- Hardware video decoding.
- Alternative geometry codecs.
- Platform-specific media stacks.
- Test or synthetic decoders.

The current FFmpeg and Draco implementations are still constructed directly by
`OpenVolumetricPlayer`, so further dependency injection and public factory
APIs would improve practical replaceability.

### 14.3 Container evolution

The version 2 `VVGF` header—the only on-disk version supported by the current
development runtime—identifies complete topology keyframes,
dependent position updates, topology IDs, referenced keyframes, decoded
vertex/triangle counts, and the payload codec. Its header-size and flags
fields leave room for compatible extensions.

Future versions could add attribute masks, alternative geometry codecs,
normal or other attribute updates, stronger topology identifiers, and
streaming-segment dependency metadata. Unknown versions and unsupported
codec/mode combinations must continue to fail explicitly.

### 14.4 Offline applications

A Nuke integration would require a different evaluation style from Unity.
Rather than continuously advancing a DSP clock, it would request deterministic
geometry and imagery for arbitrary timeline frames, including backwards and
out-of-order evaluation. The container and timestamp model can support this,
but the public core API needs a stronger pull-based, frame-at-time interface
and explicit concurrency rules.

## 15. Novel design decisions versus implementation choices

Not every component used by OpenVolumetric is itself novel. FFmpeg decoding, Draco
compression, MP4 muxing, bounded queues, and engine native plug-ins are
established techniques. The research contribution should therefore be framed
around their composition and the resulting abstraction.

### 15.1 Candidate novel design contributions

1. **A self-contained volumetric MP4 representation.** Texture video,
   conventional audio, and independently timed compressed geometry are carried
   in one interleaved MP4 rather than distributed across separately named
   files.
2. **Geometry as a first-class timed media stream.** Geometry is synchronized
   using container PTS and duration values, not filename indices, Unity
   frames, or a separately reconstructed clock.
3. **Atomic multimodal presentation.** Texture and geometry are matched by
   timestamp and uploaded as a complete presentation. If either side is
   unavailable, the previous complete presentation is retained.
4. **A host-independent volumetric playback core.** Container parsing,
   synchronization, seeking, backpressure, and decode ownership are separated
   from engine graphics resources and lifecycle.
5. **Generation-based seek and loop isolation.** Geometry work is associated
   with a timeline generation, preventing asynchronous results from an earlier
   seek or loop pass from contaminating the current presentation.
6. **A verified, integrated authoring workflow.** Source images and OBJ meshes
   are transformed into a single deliverable through an engine-facing tool
   that verifies payload identity, timestamp correspondence, and seeking
   before committing the output.
7. **Compatibility with ordinary media players.** Unknown geometry data is
   ignored while conventional video and audio remain playable, providing
   graceful degradation and straightforward preview behavior.
8. **Bounded topology reuse within the timed geometry track.** Complete Draco
   reference meshes and absolute position-only updates share one packet
   format, while author-controlled keyframe intervals bound dependency and
   future streaming seek cost.

These should be presented as design contributions subject to comparison with
related volumetric container and engine-integration systems, rather than
asserted as unprecedented without a literature review.

### 15.2 Primarily implementation choices

The following are engineering choices supporting the design, not primary
novelty claims:

- FFmpeg as the media backend.
- Draco as the geometry codec.
- AAC, HEVC, and H.264 codec selections.
- CMake and vcpkg.
- A four-second audio ring.
- Specific queue capacities.
- Eight FFmpeg decode threads.
- Unity's scheduled DSP clock and native spatializer effect.
- D3D11, Metal, and Vulkan upload implementations.
- Unity `GL.IssuePluginEvent`.
- Unreal `UDynamicMeshComponent`, transient `UTexture2D`, and
  `USoundWaveProcedural`.
- The current 65,535-vertex allocation limit.
- The `gpmd`-to-`vvge` box-tag replacement technique.

## 16. Current limitations

The most significant limitations of the current system are:

- The `vvge` MP4 representation is project-specific and provisional.
- Temporal geometry compression currently recognizes only exact canonical
  topology/UV correspondence; reordered but equivalent meshes do not match.
- Position updates reconstruct normals rather than preserving authored normal
  attributes, and broader visual/angular-error evaluation remains outstanding.
- Geometry keyframe-limit defaults and streaming-segment alignment have not
  yet been selected from broad content/device measurements.
- The managed Unity mesh has fixed vertex and triangle capacities.
- Only planar 8-bit YUV420 video output is supported by the rendering path.
- Audio is normalized to stereo float PCM; richer channel layouts are not
  exposed.
- Runtime video decoding is software-based, including on Quest.
- Progressive HTTP currently requires a known resource length and byte-range
  server support. Bounded retry, rebuffer-state publication, presentation
  freezing, audio suspension, and synchronized seek recovery are implemented,
  but Quest disconnect/reconnect and retry-exhaustion validation remain
  outstanding.
- Fragmented MP4, adaptive streaming, and live playback are not implemented.
- The authoring tool depends on an external FFmpeg executable.
- macOS and Quest validation is manual rather than an automated conformance
  suite.
- Windows requires clean-build and runtime validation after recent
  restructuring.
- Quest 3S thermal, battery, and sustained-performance evaluation is
  outstanding.
- D3D11 contains short-lived per-presentation copy threads.
- Some upload paths could reuse staging allocations more aggressively.
- The public runtime interface is optimized for continuous engine playback
  rather than arbitrary offline frame evaluation.
- Unreal currently uses CPU YUV-to-BGRA conversion and whole dynamic-mesh
  replacement rather than a direct RHI upload path.
- Unreal packaged builds and platforms beyond the macOS Editor are not yet
  validated.
- A Nuke integration remains planned.
- Error recovery generally stops or seeks the pipeline; damaged-sample
  concealment is limited.
- The current system has been evaluated on a small content set and does not
  yet establish general compression or quality results.

## 17. Summary

OpenVolumetric combines standard video, audio, and geometry technologies into
a unified timestamped volumetric-media architecture. Its central technical
distinction is not a new underlying codec but a container, synchronization,
authoring, and engine-integration design that treats dynamic geometry as timed
media alongside texture and audio.

The implementation demonstrates the complete path from numbered images and
OBJ meshes to a verified MP4 and synchronized playback in both Unity and
Unreal, including Unity standalone VR. The next steps needed for an academic
release are format stabilization, controlled cross-platform evaluation,
clean Windows and Unreal packaged-build validation, broader datasets, and
quantitative comparison of compression, decode cost, synchronization, memory
use, and sustained performance.
