# Rendering integration contracts

This document records ownership, thread, synchronization, and teardown rules
for the engine upload adapters. The engine-neutral core produces CPU-owned
presentations and never owns an engine graphics object.

## Common Unity contract

Unity creates the managed mesh and invokes the native presentation callback on
its render thread. `UnityOpenVolumetricPlayer` owns the selected native texture
and mesh uploader, but Unity owns every mesh buffer registered with it. A
successful callback copies one timestamp-matched texture/geometry presentation;
the callback does not retain decoder queue pointers. Teardown takes the native
instance-registry write lock, waits for active calls holding the shared lock,
then destroys upload resources before the player and graphics interface.

| Backend | Plugin-owned resources | Borrowed resources | Synchronization |
| --- | --- | --- | --- |
| D3D11 | Three dynamic YUV textures and their shader-resource views | Device and Unity vertex/index buffers | Render-thread `Map(WRITE_DISCARD)`, direct CPU copy, `Unmap`; driver resource renaming protects GPU readers |
| D3D12 | YUV default-heap textures and fence-tracked upload rings | Unity interface, command list, destination mesh resources | A slot is reused only after Unity's frame fence; state transitions use Unity's resource-state API |
| Metal | Private YUV textures and per-presentation shared staging buffers | Unity Metal interface, active command buffer, Unity mesh buffers | Unity's current encoder is ended before a blit encoder records copies; Objective-C command-buffer retention keeps staging alive until execution |
| Vulkan | Fallback images where required and persistently mapped staging rings | Unity Vulkan interface and registered Unity image/buffer handles | Slots are reused only at or before `safeFrameNumber`; access and layout transitions use Unity's Vulkan API |

All graphics resource creation, mapping, and access calls must be checked. A
failed operation aborts that presentation and emits a backend-specific stable
diagnostic; code must never dereference a failed mapping. Upload failure drops
the presentation rather than partially publishing texture and geometry.

## Unreal contract

`UOpenVolumetricComponent` owns Unreal objects and a private
`FOpenVolumetricPresentationUploader`. The game thread converts the matched
YUV420 presentation to BGRA and submits `UpdateTextureRegions`. The uploader
maintains three reusable byte buffers and region descriptors. A slot is marked
in flight until Unreal's render-thread cleanup callback releases it; if all
three are busy, the new texture presentation is dropped with a warning rather
than overwriting memory consumed by the render thread.

The cleanup callback captures shared upload state, not the component or
uploader. Pending render commands can therefore finish safely after component
destruction. Unreal owns the transient texture and dynamic material through
reflected object references. Destruction stops playback first, then releases
the uploader; render callbacks retain only their shared upload state.

## Performance decisions

- D3D11 performs three linear plane copies and two linear mesh-buffer copies
  directly on Unity's render thread. Short-lived worker creation and immediate
  joins added scheduling overhead without overlapping the render operation.
- Unreal reuses upload storage after its first size allocation. CPU YUV-to-BGRA
  conversion and dynamic-mesh replacement remain deliberately portable.
- The recorded Unreal profile was GPU-bound and did not isolate YUV conversion
  as the limiting cost. A planar RHI shader path is therefore deferred until a
  targeted Unreal Insights capture demonstrates enough CPU cost to justify the
  platform-specific complexity.

Windows validation must compare D3D11 frame time and presentation correctness
before and after the direct-copy change. Unreal validation should compare game
thread time and allocation events with the same content; the structural target
is zero recurring texture-upload allocations after the three slots reach their
required capacity.

### Phase 6 measurement procedure

Use the same local presentation, resolution, quality preset, engine window
size, fixed frame-rate setting, and 60-second playback interval for each run.

For Unreal, enable the developer overlay and record its `Uploads`, `dropped`,
and `storage growths` values at 10 and 60 seconds. `storage growths` may increase
while each ring slot first reaches the required resolution, then must remain
constant; `dropped` should remain zero. Capture `stat unit`, `stat memory`, and
an Unreal Insights allocation trace over the same interval. The reusable CPU
conversion array and upload slots should show no recurring presentation-size
allocations after warm-up.

For Windows Unity, build the D3D11 plugin from the commit before Phase 6 and
from the Phase 6 tree. Run the same presentation for 60 seconds in each build
and record average/main/render-thread frame time, process memory after warm-up,
and any upload errors. Repeat forward/backward seeking and two loops. Accept
the direct-copy implementation only if geometry, texture, and audio remain
synchronized and frame time does not regress materially. The expected
structural difference is zero per-frame thread creation in the Phase 6 build.

### Recorded Unreal result

The macOS Unreal Editor 5.8 validation run on 4 August 2026 produced:

| Playback time | Submitted uploads | Dropped uploads | Storage growths |
| --- | ---: | ---: | ---: |
| 10 seconds | approximately 260 | 0 | 3 |
| 60 seconds | approximately 1,800 | 0 | 3 |

The storage-growth count remained fixed after each of the three slots had
allocated its presentation-sized buffer. No upload was dropped during the
60-second run. This confirms zero recurring upload-buffer growth in steady
state and correct render-thread slot release for the tested workload.
