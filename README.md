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

Ordinary media players ignore the geometry track and continue to play the
video and audio. OpenVolumetric matches texture and geometry using MP4
presentation timestamps.

## Unity

The Unity 6 project contains:

- the `OpenVolumetric.OpenVolumetric` playback component;
- Metal, D3D11, and Vulkan native rendering paths;
- synchronized streaming audio;
- a Quest controller-operated developer overlay; and
- **Tools > OpenVolumetric > Encoder** for authoring.

See the [Unity authoring guide](docs/UNITY_AUTHORING.md) and
[Quest platform baseline](docs/QUEST_BASELINE.md).

## Unreal Engine

The Unreal Engine 5.8 plug-in contains:

- `UOpenVolumetricComponent` for C++ and Blueprint playback;
- dynamic mesh, unlit texture, and procedural audio output;
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
```

## Documentation

- [Building](docs/BUILDING.md)
- [Licensing and distribution](docs/LICENSING.md)
- [Technical overview](docs/TECHNICAL_OVERVIEW.md)
- [Native core](docs/CORE.md)
- [Authoring architecture](docs/AUTHORING.md)
- [Engine integration boundary](docs/ENGINE_INTEGRATION.md)
- [Unity authoring guide](docs/UNITY_AUTHORING.md)
- [Unreal integration guide](docs/UNREAL_INTEGRATION.md)
- [Quest platform baseline](docs/QUEST_BASELINE.md)
- [Development plan](docs/PLAN.md)
- [Shared-topology compression design](docs/TOPOLOGY_COMPRESSION.md)
- [Streaming and adaptive delivery design](docs/STREAMING_AND_ADAPTATION.md)

## Current limitations

- Geometry frames are independently Draco-compressed and do not yet exploit
  shared topology or temporal prediction.
- Playback requires a complete local or cached MP4.
- The format is project-specific and has not yet been standardized.
- Quest video decoding is software-based.
- Windows, Quest 3S, and Unreal packaged-build validation remain outstanding.
- Automated conformance and performance testing is still limited.

## Contributors

- Marco Volino — m.volino@surrey.ac.uk

## License

OpenVolumetric is licensed under the
[Apache License 2.0](LICENSE). Third-party components retain their own
licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the
[licensing guide](docs/LICENSING.md).
