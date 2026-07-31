# Container compatibility

## Supported OpenVolumetric behaviour

An OpenVolumetric MP4 contains conventional H.264 or HEVC video, optional AAC
audio, and a timed binary `vvge` geometry track. The OpenVolumetric runtime and
its pinned FFmpeg build demux all three tracks. The conventional video and
audio tracks remain independently valid and can be extracted or remuxed
without re-encoding.

Direct playback of the complete file in a generic media player is not a
supported conformance requirement. Generic-player behaviour depends on how its
MP4 demuxer handles an unknown timed data track.

## VLC limitation

VLC does not currently play the complete fragmented OpenVolumetric test files
reliably. Playback reaches the first one- or two-second fragment boundary,
then video freezes, audio stops, and the displayed timeline returns to zero.
VLC does not report a useful error.

Testing on 31 July 2026 isolated the behaviour as follows:

| File variant | VLC result |
|---|---|
| Complete fragmented OpenVolumetric MP4 | Stops at first fragment boundary |
| Fragmented MP4 with only the original video and audio | Plays; seeking is less robust than the flat file |
| Flat MP4 with only the original video and audio | Plays normally |
| Complete file with the auxiliary `gpmd` declaration restored | Same failure |
| Complete file with all geometry `tkhd` presentation flags cleared | Same failure |

FFprobe reports continuous zero-based timestamps and matching approximately
120.9-second video, audio, and geometry durations. FFmpeg decodes the media
tracks and discovers the `vvge` stream, while Unity on macOS and Quest and the
Unreal integration play and seek the complete fragmented file correctly. The
evidence therefore identifies a VLC/custom-fragmented-data-track
interoperability limitation rather than corrupt video or audio encoding.

## Why the track is custom

The pinned FFmpeg 8.1.2 MP4 muxer exposes arbitrary binary data through its
GoPro `gpmd` sample entry. OpenVolumetric uses FFmpeg for muxing and performs an
equal-size declaration change to the unambiguous `vvge` identifier. FFmpeg does
not currently provide an authoring path for a generic ISO BMFF `mett` or `metx`
timed-metadata entry carrying these binary packets.

Replacing `vvge` with standardized timed metadata would require a separately
implemented sample entry, a packaging dependency such as GPAC/MP4Box, or a
maintained FFmpeg extension. That is deferred until its interoperability value
can be evaluated against the added dependency and maintenance cost.

## Previewing conventional media

For inspection in a player that does not tolerate `vvge`, remux only the video
and optional audio streams. This does not decode or re-encode either stream:

```sh
ffmpeg -i input.mp4 -map 0:v:0 -map 0:a:0? -c copy preview.mp4
```

The preview is not volumetric because it intentionally omits geometry.
