# OpenVolumetric synchronization test

This deterministic ten-second sequence provides visible and audible events at
exact one-second boundaries:

- the texture flashes white for three frames;
- the constant-topology cube expands from scale 1.0 to 1.25 for those frames;
- a 20 ms stereo click begins on the same boundary. The timestamp-zero click
  is 500 Hz, while all subsequent clicks are 1 kHz, making a lost or delayed
  startup event easy to distinguish.

The cube contains explicit UVs and face normals. Its 24 render vertices keep
normal/UV seams stable so the same source can test independent meshes and
topology-compressed position updates.

Generate the assets from the repository root:

```bash
python3 data/sync-test/generate.py
```

The output directories are suitable for the OpenVolumetric authoring window:

- **Image Sequence:** `data/sync-test/texture`
- **OBJ Sequence:** `data/sync-test/geometry`
- **Audio:** `data/sync-test/click.wav`
- **Source Frame Rate:** `30`

Encode with any platform preset. During playback, each click should coincide
with the first white/expanded frame at frames 0, 30, 60, and so on. Record the
screen and speaker together at a high frame rate for an end-to-end latency
measurement. Repeating the test after seeking and looping distinguishes a
fixed startup offset from accumulated clock drift.
