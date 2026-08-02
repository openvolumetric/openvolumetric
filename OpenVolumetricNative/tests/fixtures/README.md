# Native playback fixture

The fixture family is deterministic, one second long, and four frames at 4 FPS:

- `openvolumetric-test.mp4` uses temporal geometry and AAC audio;
- `openvolumetric-independent.mp4` uses an independent Draco mesh per frame;
- `openvolumetric-corrupt-update.mp4` corrupts the first dependent Draco
  payload while retaining the following independent recovery frame;
- `openvolumetric-no-audio.mp4` omits the audio track;
- `openvolumetric-fragmented.mp4` is a one-fragment fMP4; and
- `adaptive/` contains a two-representation manifest package based on the
  fragmented fixture.

The primary lifecycle fixture contains:

- a 16 x 16, 4 FPS H.264 texture track;
- a 48 kHz AAC audio track; and
- four `vvge` geometry samples representing one moving textured triangle.

The geometry uses temporal compression with independent frames at samples zero
and two and position-only updates at samples one and three. This deliberately
exercises both geometry coding modes and a bounded reference window.

The checked-in files are deliberately small so clean builds can run playback
tests without the full example dataset. Regenerate all variants after an
intentional packet or container format change by building the test-only
generator and supplying an FFmpeg
executable with libx264 and AAC support:

```sh
cmake --build build/vcpkg-Darwin --target OpenVolumetricFixtureGenerator
tests/fixtures/regenerate_fixture.sh \
  build/ffmpeg/bin/ffmpeg \
  build/vcpkg-Darwin/tests/OpenVolumetricFixtureGenerator
```

Run the complete test suite after regeneration. Review and commit the changed
fixture only when the format change is intentional.
