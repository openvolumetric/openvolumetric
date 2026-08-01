# OpenVolumetric for Unreal Engine

The OpenVolumetric plug-in provides runtime playback and Editor authoring for
Unreal Engine 5.8. It uses the same `OpenVolumetricCore` and MP4 format as the
Unity integration.

## Modules

- `OpenVolumetricRuntime` exposes `UOpenVolumetricComponent` to C++ and
  Blueprints and links the engine-independent decoder core.
- `OpenVolumetricAuthoring` adds the OpenVolumetric Encoder to Unreal's Tools
  menu and links the reusable authoring core.

## Runtime usage

1. Enable the OpenVolumetric plug-in and restart the Editor if requested.
2. Add an Actor to the level.
3. Add an **Open Volumetric Component** to the Actor.
4. Set **Source File** to an OpenVolumetric MP4, or set **Source URL** to an
   HTTP(S) fast-start MP4. Source URL takes precedence when both are present.
5. Enable **Play on Open** and **Loop** as required.
6. Enter Play in Editor.

### Developer controls

**Enable Developer Controls** creates a small engine on-screen status display
without any Blueprint, UMG, or level UI setup. Click the game viewport so it
has keyboard focus, then use:

- **K**: play or pause; Play at the end restarts from zero;
- **J**: seek backward by 10 seconds;
- **L**: seek forward by 10 seconds;
- **O**: toggle looping; and
- **I**: hide or show the panel.

The seek interval is configurable through **Developer Seek Seconds**. The panel
shows playback state and time, loop state, frame rate, frame time, process
memory, errors, and HTTP cache, request, recovery, and fragment diagnostics
when applicable. Disable **Enable Developer Controls** for a release-facing
player with no built-in diagnostics or shortcuts.

The component creates and owns its dynamic mesh, transient texture, unlit
material instance, procedural sound wave, and audio component. Playback
operations are also available to Blueprints:

The **Enable Geometry Centroid Spatial Audio** option places the procedural
audio component at the centroid of each presented mesh. **Spatial Audio
Smoothing Seconds** reduces small frame-to-frame source-position changes.
This changes only the audio component transform: PCM queueing and the shared
playback clock remain untouched. Unreal's configured spatializer performs the
actual rendering, while distance attenuation remains disabled by default so
enabling the option does not unexpectedly alter playback level.

- `Open`
- `Play`
- `Pause`
- `Seek`
- `Close`

Status, duration, current time, and the last error are exposed as component
properties. Remote playback additionally publishes resource size, cache
occupancy, cumulative downloaded bytes, and HTTP request count beneath
**Status > Buffer**.

The component's **Texture** section also exposes the same normalized
correction controls as Unity:

- **Luminance Correction** offsets the Y channel;
- **Blue Projection Correction** offsets the U channel; and
- **Red Projection Correction** offsets the V channel.

Each control ranges from -0.2 to 0.2 and is applied before BT.601 RGB
conversion.

The supplied material is two-sided and unlit. The component disables shadows,
emissive-light-source behavior, and dynamic indirect-light contribution so
the captured texture is displayed without scene relighting.

## Authoring

Open **Tools > OpenVolumetric Encoder**. Select:

- a contiguous numbered image sequence;
- a matching numbered OBJ sequence;
- optional audio;
- an output MP4 path for fixed-quality encoding, or an output parent folder
  and presentation name for adaptive encoding; and
- a platform preset or custom encoding settings.

OBJ encoding uses Draco linked into the authoring module. Texture and audio
encoding require an external FFmpeg executable with the selected H.264/HEVC
encoder and AAC support. Packaging and verification run through
`OpenVolumetricAuthoringCore`.

Enable **Adaptive Package** with either streaming preset to author a coupled
two-level ladder. The presentation name `presentation` creates a
`presentation` directory containing `manifest.json`, `low.mp4`, and
`high.mp4`. Both representations use the selected 1-, 2-, or 4-second
fragment duration and share aligned video and full-geometry access points.
The native verifier rejects mismatched durations, fragment counts, or audio
layouts before writing the manifest.

The **Geometry Compression** checkbox enables topology-aware position updates.
Clearing it emits the same packet format with every sample encoded as an
independently decodable Draco mesh.

The optional **Limit Geometry Keyframes** checkbox and **Maximum Geometry
Frames** value force periodic complete Draco reference meshes. The limit
includes the reference frame itself and is separate from the video keyframe
interval. Leave it disabled to reuse matching topology until it changes.

## Current platform status

Runtime geometry, texture, and audio playback and the authoring window are
implemented and manually validated in Unreal Editor 5.8 on macOS ARM64.
End-to-end authoring was validated on 28 July 2026 using 3,627 matched
OBJ/JPEG frames and MP3 audio, including Draco encoding, HEVC/AAC encoding,
MP4 packaging, and verification.

Adaptive authoring was validated on 1 August 2026 against Unity using the
same source and Quest Streaming ladder. Unreal produced byte-identical low
and high MP4 representations and an equivalent manifest; only the expected
presentation-derived identifiers and resource filenames differed.

Still outstanding:

- packaged-build dependency staging and validation;
- Windows and additional RHI/platform integrations;
- RHI-native YUV conversion and direct GPU upload;
- comprehensive loop, seek, lifecycle, and corrupt-input testing; and
- Unreal Android/Quest support.

The sample project level is `/Game/OpenVolumetricSample`.
