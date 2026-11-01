# Nuke Integration Validation

This checklist validates the OpenVolumetric reader against Foundry Nuke
17.1v1 on macOS ARM64. Use a combined local OpenVolumetric MP4 containing
texture and geometry; audio remains carried in the file but is not presented
by the Nuke reader.

## Reader and timeline correctness

1. Create `OpenVolumetricRead`, select the MP4, and inspect the first, middle,
   and final source frames.
2. Play forward, play in reverse, scrub repeatedly in both directions, and
   jump between widely separated frames.
3. Move before **timeline start** and beyond the placed trim duration. With
   `hold`, the nearest valid trim-boundary presentation must remain visible
   without an error or crash.
4. Render a short frame range through `ScanlineRender` and `Write`. Compare
   representative output frames with the interactive Viewer.
5. Cancel a frame-range render and verify Nuke becomes responsive promptly.

Texture and geometry must always describe the same presentation. Continuous
3D Viewer playback can briefly display Nuke's grey fallback material while a
frame with new topology and its material resources are rebuilt. Deterministic
random access and rendered frames must not contain that fallback.

## Timing and trim validation

Verify that **source range** reports the native frame range, frame rate, and
duration after selecting an MP4. Adjust both **trim in** and **trim out**, then
set **timeline start** to both positive and negative values. Geometry and
texture must remain on the same frame.

Test `none` and `hold` independently for **before** and **after**. `none` must
produce no geometry and a black/transparent texture outside the placed range;
`hold` must retain the complete nearest boundary frame. Also test a one-frame
trim, random access, a frame-range render, clone/copy-paste, and save/reopen.

## Cache and shared-session lifecycle

1. Create two readers using the same MP4 and scrub both. They must remain
   independent at the node level while sharing one native decoder session.
   Translate one reader from the **Transform** tab and verify both instances
   are visible at different positions when combined through `GeoScene`.
2. Clone a reader, change its timing controls, and verify both nodes remain
   correct.
3. Create another reader using a different MP4 and verify its frames cannot
   appear in the first reader.
4. Change the cache frame and memory limits, then use **reload source**. The
   reader must reopen cleanly without affecting a reader using another cache
   configuration.
5. Delete readers during idle operation and after scrubbing. The final
   consumer releases the shared session, decoder threads, and presentation
   cache; Nuke must remain responsive.

The process-global registry holds weak references only. Geometry and texture
nodes retain a session while they use it, but the registry cannot extend its
lifetime after the final consumer is destroyed.

## Project and application lifecycle

1. Save the script, close it, reopen it, and confirm source and control values.
2. Copy and paste the reader, then delete the original and the copy.
3. Create a reader with no source and one with an invalid source; both must
   report an actionable error and remain safely deletable.
4. Close Nuke normally after playback, random access, a cancelled render, and
   source reload. No crash or lingering Nuke process is permitted.

## Temporal geometry and damaged input

1. Seek to dependent temporal-geometry frames after several different
   reference windows. Their reconstructed positions must match sequential
   playback.
2. Test a controlled file with one damaged dependent geometry sample. The
   request must fail explicitly and a later valid reference must allow
   subsequent random access to recover.
3. Test a truncated or missing source. Nuke must report failure without
   publishing mixed texture and geometry or crashing.

## Regression evidence

Record the Nuke version, macOS version, machine architecture, source preset,
cache settings, test date, and pass/fail result. Also run the native core and
HTTP transport tests. Unity, Unreal, Android/Quest, Windows, and authoring
validation remain separate platform procedures.
