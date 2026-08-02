# Open Volumetric

OpenVolumetric is an open, cross-platform volumetric-video authoring and
playback project. It stores texture video, time-varying Draco geometry, and
optional audio in one conventional MP4 and decodes the same file through
Unity and Unreal Engine integrations.

## Current status

| Host/platform | Status |
| --- | --- |
| Unity 6 on macOS/Metal | Implemented and validated |
| Unity 6 on Meta Quest/Android ARM64/Vulkan | Implemented and validated on Quest 2 |
| Unity on Windows/D3D11 | Implemented; clean validation outstanding |
| Unreal Engine 5.8 on macOS | Runtime playback and authoring implemented and validated in the Editor |
| Unreal packaged applications | Packaging validation outstanding |
| Linux | Portable native core build |

Both engine integrations play the same OpenVolumetric MP4 without format
conversion.

## Format

An OpenVolumetric MP4 contains:

- an HEVC or H.264 texture-video track;
- a timed `vvge` track containing versioned Draco geometry packets; and
- an optional conventional audio track.

OpenVolumetric matches texture and geometry using MP4 presentation timestamps.
The conventional video and audio remain valid and can be remuxed for ordinary
preview, but not every generic player ignores the custom track correctly; see
[container compatibility](docs/CONTAINER_COMPATIBILITY.md).

## Unity

The Unity 6 project contains:

- the `OpenVolumetric.OpenVolumetric` playback component;
- Metal, D3D11, and Vulkan native rendering paths;
- synchronized streaming audio, with optional geometry-centroid following and
  lightweight native stereo spatialisation validated on Quest for local and
  HTTP-streamed input;
- a Quest controller-operated developer overlay; and
- **Tools > OpenVolumetric > Encoder** for authoring.

See the [Unity authoring guide](docs/UNITY_AUTHORING.md) and
[Quest platform baseline](docs/QUEST_BASELINE.md).

## Unreal Engine

The Unreal Engine 5.8 plug-in contains:

- `UOpenVolumetricComponent` for C++ and Blueprint playback;
- dynamic mesh, unlit texture, and procedural audio output with optional
  geometry-centroid spatialisation;
- an optional keyboard-operated runtime developer panel;
- **Tools > OpenVolumetric Encoder** for authoring; and
- the `/Game/OpenVolumetricSample` example level.

See the [Unreal integration guide](docs/UNREAL_INTEGRATION.md).

## Repository layout

```text
OpenVolumetricNative/   Engine-independent runtime and authoring C++ libraries
Unity/                  Unity project and integration
Unreal/                 Unreal project and plug-in
docs/                   Architecture, build, platform, and roadmap documents
data/                   Local sample content
tools/                  Package-serving and controlled-network evaluation tools
```

## Documentation

- [Building](docs/BUILDING.md)
- [Testing and engine acceptance](docs/TESTING.md)
- [Licensing and distribution](docs/LICENSING.md)
- [Technical overview](docs/TECHNICAL_OVERVIEW.md)
- [Container compatibility](docs/CONTAINER_COMPATIBILITY.md)
- [Native core](docs/CORE.md)
- [Authoring architecture](docs/AUTHORING.md)
- [Engine integration boundary](docs/ENGINE_INTEGRATION.md)
- [Unity authoring guide](docs/UNITY_AUTHORING.md)
- [Unreal integration guide](docs/UNREAL_INTEGRATION.md)
- [Quest platform baseline](docs/QUEST_BASELINE.md)
- [Development plan](docs/PLAN.md)
- [Shared-topology compression design](docs/TOPOLOGY_COMPRESSION.md)
- [Geometry packet format](docs/GEOMETRY_PACKET.md)
- [Streaming and adaptive delivery design](docs/STREAMING_AND_ADAPTATION.md)
- [Adaptive streaming evaluation](docs/ADAPTIVE_EVALUATION.md)

## Current limitations

- Geometry compression reuses matching topology through Draco position-only
  updates, but its compression, reconstruction quality, and keyframe-interval
  defaults still need broader evaluation.
- Local, progressive HTTP, fixed fragmented HTTP, and adaptive low/high
  multi-representation playback are implemented; full controlled-network and
  cross-platform evaluation remains in progress.
- The format is project-specific and has not yet been standardized.
- VLC does not reliably ignore `vvge` in fragmented files; video/audio-only
  stream-copy previews remain available without re-encoding.
- Quest video decoding is software-based.
- Windows, Quest 3S, and Unreal packaged-build validation remain outstanding.
- Native format, lifecycle, transport, corruption-recovery, and sanitizer
  regression checks are automated; engine conformance and performance testing
  remains manual.

## Contributors

- Marco Volino — m.volino@surrey.ac.uk

## License

OpenVolumetric is licensed under the
[Apache License 2.0](LICENSE). Third-party components retain their own
licenses; see the [third-party notices](docs/THIRD_PARTY_NOTICES.md) and the
[licensing guide](docs/LICENSING.md).
