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

The component creates and owns its dynamic mesh, transient texture, unlit
material instance, procedural sound wave, and audio component. Playback
operations are also available to Blueprints:

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
- an output MP4 path; and
- a platform preset or custom encoding settings.

OBJ encoding uses Draco linked into the authoring module. Texture and audio
encoding require an external FFmpeg executable with the selected H.264/HEVC
encoder and AAC support. Packaging and verification run through
`OpenVolumetricAuthoringCore`.

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

Still outstanding:

- packaged-build dependency staging and validation;
- Windows and additional RHI/platform integrations;
- RHI-native YUV conversion and direct GPU upload;
- comprehensive loop, seek, lifecycle, and corrupt-input testing; and
- Unreal Android/Quest support.

The sample project level is `/Game/OpenVolumetricSample`.
