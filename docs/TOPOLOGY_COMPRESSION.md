# Shared-topology geometry compression

## Status

This document records a proposed future extension to OpenVolumetric. It is not
implemented in the current version-1 geometry format.

The objective is to reduce geometry size when consecutive meshes have the
same topology and UV layout. A complete Draco mesh establishes reusable
topology; dependent frames carry only updated vertex positions. When topology
or UVs change every frame, encoding falls back to the current independent
Draco representation.

## Design principles

- Preserve version-1 independent-frame playback.
- Require deterministic vertex correspondence before reusing topology.
- Keep compression and reconstruction inside the engine-independent core.
- Continue exposing complete engine-neutral meshes to Unity and Unreal during
  the first implementation.
- Bound dependency length, memory use, seek preroll, and corruption impact.
- Prefer robust random access over maximum compression in the first version.
- Measure compression and reconstruction error rather than assuming a
  residual codec is beneficial.

## Topology identity

Equal vertex and triangle counts are not sufficient. Two frames can share
topology only when all of the following match:

- vertex count;
- triangle count;
- exact triangle-index order;
- winding order;
- UV count;
- vertex-to-UV mapping;
- quantized UV values; and
- required attribute presence and layout.

Positions and normals are excluded because they are expected to change.

The authoring pipeline should build a canonical render mesh from each OBJ
before fingerprinting it. OBJ corner tuples must map deterministically to
render vertices so UV seams produce the same vertex expansion on every frame.

For the initial implementation, vertex and index ordering must match exactly.
Recognizing equivalent meshes with reordered vertices requires a separate
correspondence algorithm and is outside the first scope.

UV values should be deterministically quantized before hashing so
insignificant floating-point serialization differences do not split an
otherwise reusable window.

Conceptually:

```text
topology_id = hash(
    format_version,
    vertex_count,
    triangle_indices,
    winding,
    quantized_uvs,
    attribute_mapping
)
```

Hash equality is an index into a topology candidate, not the final proof.
Before reuse, the encoder should compare the canonical topology data to avoid
depending solely on collision resistance.

## Geometry packet modes

Version 2 of the `VVGF` packet should support at least:

```text
IndependentMesh
├── Complete Draco mesh
├── Topology ID
└── Existing frame and payload validation

PositionUpdate
├── Referenced topology ID
├── Referenced topology keyframe/sample
├── Vertex count
├── Position quantization metadata
├── Encoded and decoded sizes
└── Compressed position residuals
```

Suggested header information:

- packet-format version;
- packet mode/flags;
- topology ID;
- frame number;
- referenced keyframe or sample;
- vertex count;
- quantization bounds and precision;
- encoded payload size;
- expected decoded size; and
- optional checksum.

Version-1 `VVGF` packets remain independent meshes and must continue to
decode without conversion.

## Position representation

For each shared-topology window:

1. Encode the first frame as a complete Draco topology keyframe.
2. Select fixed position bounds and quantization for the window.
3. Quantize each dependent frame's positions using those settings.
4. Subtract the quantized topology-keyframe positions.
5. Zigzag-encode the signed integer residuals.
6. Compress the residual stream with the selected backend.

Keyframe-relative residuals are recommended initially instead of
previous-frame residuals:

- reconstruction does not accumulate drift;
- every dependent frame has one bounded dependency;
- a corrupt dependent frame does not invalidate later dependent frames;
- decoding can begin directly after loading the topology keyframe; and
- dependent frames can be decoded independently or in parallel.

Previous-frame prediction can be evaluated later if its compression gain
justifies dependency chains and more complicated recovery.

Zstandard is a practical initial compression candidate, but it should be
benchmarked against raw packed residuals, LZ4, and a simple entropy-coded
representation. The format should identify its payload codec rather than
hard-coding an undocumented assumption.

## Normal handling

If dependent packets contain only positions, normals must be reconstructed.
The first implementation should:

1. Restore positions from the keyframe-relative residual.
2. Reuse cached triangle indices and UVs.
3. Recalculate face normals.
4. Accumulate and normalize vertex normals using a documented weighting
   method.

Recomputed normals may differ from source OBJ normals. Verification must
measure maximum and average angular error and include visual comparison.

An optional later packet mode can encode normal residuals or complete
quantized normals when preserving authored/captured normals is important.

## Authoring analysis

Before encoding geometry, the authoring pipeline should:

```text
OBJ sequence
    |
    v
Canonical mesh extraction
    |
    v
Topology and UV fingerprint per frame
    |
    v
Segment consecutive matching fingerprints
    |
    +-- one frame --------> IndependentMesh
    |
    `-- reusable window --> topology keyframe + PositionUpdate packets
```

Automatic mode should consider the total encoded result. A short matching
window may not save enough space to justify new headers, quantization
metadata, and keyframe constraints. The encoder may retain independent Draco
when that is smaller or simpler.

Proposed authoring controls:

- mode: automatic, independent only, or topology reuse;
- maximum topology-window length;
- minimum reusable-window length;
- forced topology-keyframe interval;
- position quantization precision or error target;
- residual compression backend/level;
- normal mode: recalculate or encode; and
- force-independent frame markers for testing.

If topology changes on every frame, automatic mode emits only independent
Draco packets and preserves current behavior.

## Core decoding and topology cache

The geometry decoder should maintain a bounded cache entry for each active
topology:

```text
TopologyCacheEntry
├── Topology ID
├── Vertex and triangle counts
├── Indices
├── UVs and attribute mapping
├── Keyframe quantized positions
├── Quantization metadata
└── Playback generation / last use
```

Decoding behavior:

- `IndependentMesh`: decode through Draco, publish the full mesh, and populate
  or replace the topology cache entry.
- `PositionUpdate`: find and validate the referenced topology, decompress and
  reconstruct positions, recalculate normals, and publish a complete mesh.
- Missing topology: return a controlled error or require seek preroll; never
  apply an update to an unrelated cache entry.
- Corrupt update: discard that presentation and recover at the next valid
  dependent packet or topology keyframe according to the final policy.
- Seek or loop: isolate cache entries by playback generation so stale
  asynchronous results cannot cross a reset.

Cache memory must be bounded by entry count and decoded byte size. Entries
should be evicted only when they cannot be referenced by queued packets.

## Seeking and random access

A seek into a dependent window requires the referenced topology keyframe.

The container and packet metadata must permit the runtime to:

1. Find the preceding required video and topology keyframes.
2. Decode/cache the topology keyframe.
3. Decode the requested keyframe-relative position update.
4. Complete normal reconstruction.
5. Present only when matching texture and geometry timestamps are ready.

Because updates are keyframe-relative, intermediate geometry frames do not
need to be decoded solely to reconstruct the target. Periodic topology
keyframes should bound seek cost even when topology remains constant for an
entire sequence.

## Engine integration

The first implementation should reconstruct an ordinary complete `Mesh` in
`OpenVolumetricCore`. Unity and Unreal therefore require no format knowledge
and can be validated before engine-specific optimization.

A later presentation API can expose:

```cpp
struct MeshPresentation
{
    bool topology_changed;
    positions;
    normals;
    indices;
    uvs;
};
```

When `topology_changed` is false:

- Unity can retain its index/UV buffers and update only positions and normals.
- Unreal can avoid reconstructing the complete `FDynamicMesh3` topology.
- Both engines can reduce CPU copies and graphics-buffer traffic.

This optimization is separate from the container change and should be
implemented only after decoded output is proven equivalent.

## Validation

The authoring verifier must decode every output frame and compare it with the
canonical source:

- vertex and triangle counts;
- exact triangle indices and UV mapping;
- maximum and RMS position error;
- maximum and average normal angular error;
- bounding boxes;
- topology IDs and packet references;
- payload sizes and decoded-size checks;
- timestamp and duration correspondence;
- random seeks into every shared-topology window; and
- loop-boundary reconstruction.

Required test sequences:

- constant topology for the complete sequence;
- several short reusable windows;
- alternating topology;
- topology changing every frame;
- identical counts but different indices;
- identical indices but changed UVs;
- UV values differing only below the quantization threshold;
- corrupt or missing position update;
- corrupt or missing topology keyframe;
- seek directly into every dependent window; and
- seek, loop, and generation reset while a dependent frame is decoding.

## Evaluation

Compare the new mode with independent Draco using:

- total geometry-track size and complete MP4 size;
- bytes per geometry frame;
- compression ratio by window length;
- authoring time;
- geometry decode and normal-reconstruction time;
- peak and steady-state memory;
- seek latency;
- position and normal error;
- Unity and Unreal CPU/render-thread cost; and
- sustained playback behavior on desktop and Quest-class hardware.

Report cases where topology reuse is larger or slower and confirm that
automatic mode selects independent Draco for them.

## Implementation phases

### Phase 1: Analysis only

- Implement canonical mesh extraction.
- Implement UV quantization and topology fingerprints.
- Report reusable windows and estimated position payload sizes.
- Do not change the file format or runtime.

### Phase 2: Version-2 packets

- Define and document independent and position-update headers.
- Add packet parsing and validation tests.
- Preserve version-1 decoding.

### Phase 3: Authoring

- Segment matching-topology windows.
- Emit periodic Draco topology keyframes.
- Encode keyframe-relative quantized position residuals.
- Add full round-trip verification and automatic fallback.

### Phase 4: Core decoding

- Add the bounded topology cache.
- Reconstruct positions and normals.
- Integrate playback generations, seeking, looping, EOS, and error handling.

### Phase 5: Engine optimization

- First validate complete reconstructed meshes in Unity and Unreal.
- Then add topology-stable position/normal-only update paths.

### Phase 6: Evaluation

- Benchmark compression, error, memory, decode time, seek latency, and engine
  performance.
- Select default window/keyframe and codec settings from measurements.

## Recommended initial choices

Unless Phase 1 evidence indicates otherwise, begin with:

- exact canonical vertex and index correspondence;
- deterministically quantized UV fingerprints;
- version-1 packets treated as independent meshes;
- complete Draco topology keyframes;
- keyframe-relative quantized integer position residuals;
- recalculated normals;
- a bounded maximum window/keyframe interval;
- Zstandard as the first residual-codec experiment; and
- automatic independent-Draco fallback whenever topology differs or reuse is
  not beneficial.
