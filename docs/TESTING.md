# Testing OpenVolumetric

OpenVolumetric separates fast native regression tests from engine acceptance
tests. CTest is authoritative for deterministic core behavior; Unity, Quest,
and Unreal checks remain manual because they require licensed editors, graphics
devices, audio devices, or headset hardware.

## Automated native checks

```sh
cd OpenVolumetricNative
cmake --preset vcpkg
cmake --build --preset vcpkg
ctest --preset vcpkg
```

The suite covers packet and manifest validation, topology identity, local and
HTTP byte sources, lifecycle rollback, independent and temporal geometry,
no-audio and fragmented inputs, adaptive package resolution, dependent-frame
seek recovery, corrupt-dependent recovery at the next independent mesh, and
end-of-stream restart. The HTTP harness uses an ephemeral loopback server and
tests range reads, retry recovery/exhaustion, truncation, and cancellation.

The checked-in media is intentionally tiny. See
`OpenVolumetricNative/tests/fixtures/README.md` for its content and exact
regeneration procedure. Full sample and benchmark media stays outside normal
Git history.

On Clang or GCC hosts, run memory and undefined-behavior checks with:

```sh
cmake --preset vcpkg-sanitizers
cmake --build --preset vcpkg-sanitizers
ctest --preset vcpkg-sanitizers
```

GitHub Actions repeats warnings-as-errors builds and native tests on Linux,
macOS, and Windows, runs ASan/UBSan on Linux, and builds Android ARM64. Engine
projects are deliberately excluded from these fast jobs.

## Manual engine acceptance

Use the same known-good conventional, fragmented, and two-quality adaptive
packages in each applicable host. Record engine/version, OS/device, graphics
API, package preset, source URL or local path, and outcome.

### Unity Editor on macOS or Windows

1. Open a local conventional file and verify texture, geometry, and native-DSP
   spatial audio start together.
2. Pause/resume, seek forward and backward, toggle looping, run two loops, and
   restart after a non-looping end.
3. Repeat with a fragmented file over HTTP.
4. Open an adaptive manifest over HTTP; exercise Low, High, Auto, and a manual
   representation switch. Confirm no mixed texture/geometry presentation.
5. Briefly interrupt transport and confirm synchronized recovery without a
   crash or unbounded cache growth.

### Quest

1. Build ARM64 with Vulkan and run the Quest streaming preset on the target
   headset.
2. Repeat play/pause, forward/backward seek, looping, end/restart, spatial
   audio, and adaptive switching over Wi-Fi.
3. Interrupt Wi-Fi and confirm the overlay reports rebuffer/recovery and all
   modalities resume together.
4. Run for at least ten minutes and record frame rate, thermal behavior,
   stalls, audio underruns, and memory trend.

### Unreal Editor

1. Rebuild the native archive before the Unreal modules, then open the same
   local conventional and fragmented packages.
2. Use the developer panel for play/pause, seek, loop, end/restart, and panel
   visibility; exiting PIE must not crash.
3. Set `SourceUrl` to the HTTP manifest, enable adaptive input, and repeat Low,
   High, Auto, manual switching, and brief network interruption.
4. Confirm texture controls, two-sided unlit rendering, spatial audio, and
   texture/geometry/audio synchronization remain correct.

Engine acceptance failures should include the engine log, source mode,
representation, playback time, and developer-overlay transport state.
