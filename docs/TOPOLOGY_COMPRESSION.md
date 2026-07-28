# Shared-topology geometry compression

## Status

This document records the implemented shared-topology extension and its
supporting experiments. The concrete wire format is recorded separately in
[GEOMETRY_PACKET.md](GEOMETRY_PACKET.md).

The objective is to reduce geometry size when consecutive meshes have the
same topology and UV layout. A complete Draco mesh establishes reusable
topology; dependent frames carry only updated vertex positions. When topology
or UVs change every frame, encoding falls back to the current independent
Draco representation.

## Design principles

- Use one packet format for independent and topology-reusing coding modes.
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

The `VVGF` packet supports:

```text
IndependentMesh
├── Complete Draco mesh
├── Topology ID
└── Existing frame and payload validation

PositionUpdate
├── Referenced topology ID
├── Referenced topology keyframe/sample
├── Vertex count
└── Sequential Draco position point cloud
```

Suggested header information:

- packet-format version;
- packet mode/flags;
- topology ID;
- frame number;
- referenced keyframe or sample;
- vertex count;
- encoded payload size;
- expected vertex and triangle counts.

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

`OpenVolumetricAuthoringCore` now contains the initial analysis implementation
in `TopologyAnalyzer`. It canonicalizes OBJ position/UV corner tuples, creates
a deterministic topology ID, performs an exact comparison after a hash match,
segments consecutive matches, and estimates packed position bytes. It does
not yet alter encoded output.

The repository does not contain the original raw OBJ sequence, so only
synthetic correctness fixtures can currently run from a clean checkout. The
representative sequence analysis required by Milestone 10 must be run when the
source OBJ dataset is available; packaged MP4 and independently encoded Draco
files are not a substitute for validating authoring-time vertex
correspondence.

### Reconstructed capture result

For development analysis, the 3,627 independently encoded Draco samples were
decoded back to OBJ and analysed on 28 July 2026. Frames `000110` through
`003736` produced:

- 3,627 topology runs;
- zero reusable dependent frames;
- a minimum and maximum run length of one frame; and
- no estimated position-payload saving from topology reuse.

This confirms that the captured sequence exercises the required
independent-frame fallback: its reconstructed topology and/or UV layout
changes every frame. It cannot be used to select a temporal residual codec.
A controlled fixed-topology deformation sequence is required for the codec
benchmark, followed by evaluation on additional naturally fixed-topology
content when available.

### Initial controlled residual benchmark

An optional internal benchmark now creates 119 dependent deformations from
the real `000110.obj` mesh (6,479 render vertices and 12,000 triangles). It
quantizes each dependent frame against fixed window bounds, computes
keyframe-relative signed residuals, verifies every codec round trip, and
measures reconstructed-position error.

The first macOS ARM64 results were:

| Position bits | RMS component error | Maximum component error | Varint | LZ4 int32 | Zstd-1 int32 | Zstd-3 int32 | Zstd-1 varint |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 | 0.00007445 | 0.00020730 | 2,924,053 B | 4,275,241 B | 2,691,007 B | 2,622,573 B | 2,393,318 B |
| 14 | 0.00001861 | 0.00005186 | 4,143,596 B | 5,286,802 B | 3,670,665 B | 3,642,482 B | 3,303,723 B |
| 16 | 0.00000465 | 0.00001299 | 4,509,955 B | 7,164,527 B | 4,718,084 B | 4,641,851 B | 3,921,135 B |

The uncompressed dependent float positions occupy 9,252,012 bytes in every
case. At 14 bits, Zstandard level 1 over zigzag varints used 35.7% of that
baseline. Its complete measured encode/decode cost for all 119 frames was
approximately 5.27/4.05 ms on the development Mac, or about 0.044/0.034 ms
per dependent frame. Zstandard level 3 saved less than one percent versus
level 1 on raw 14-bit residuals while taking longer to encode.

These results make zigzag varints plus Zstandard level 1 the leading initial
candidate, with plain varints retained as a lower-complexity fallback. They
do not freeze the format: the deformation is controlled rather than captured
fixed-topology content, the baseline is uncompressed float positions rather
than independently encoded Draco, and normal reconstruction is not included.
Those gaps must be measured before the Milestone 10 codec decision is final.

### Draco position-only experiment

The first controlled experiment uses the 150-frame, five-window fixture under
`data/benchmark/windowed_topology`. A complete Draco mesh begins each topology
window. Every subsequent frame is represented as a Draco point cloud containing
only the canonical render-vertex positions.

Two Draco point-cloud modes were tested at 14-bit position precision:

| Mode | Total for 150 frames | Mean/frame | Decode total | Stable point order |
| --- | ---: | ---: | ---: | --- |
| Sequential | 3,224,968 bytes | 21,499 bytes | 20.25 ms | Yes, 150/150 |
| KD-tree | 3,160,461 bytes | 21,069 bytes | 34.51 ms | No, 0/150 |

The sequential mode produced an RMS component error of approximately
`0.000029` and a maximum component error of `0.000051`. Its decoded point order
matched the canonical render-vertex order in every tested frame. The KD-tree
mode was only about two percent smaller, but reordered the points in every
frame. Its decoded data would therefore require an additional vertex-ID
attribute or remapping table.

This result makes sequential Draco point clouds the current preferred update
representation. It reuses the existing dependency, remains independently
decodable, and can update cached triangle indices and UVs without a custom
residual codec.

Default EdgeBreaker Draco meshes reordered all five controlled keyframes.
Topology keyframes must therefore use Draco's sequential mesh mode as well as
position updates. The packer validates decoded keyframe indices against the
canonical OBJ before emitting version 2.

The corrected complete size comparison produced:

- 150 independent sequential Draco meshes: `16,982,258` bytes;
- five sequential complete-mesh topology keyframes: `534,331` bytes;
- 145 sequential Draco position updates: `3,149,270` bytes;
- projected topology-aware geometry payload: `3,683,601` bytes.

This is a `78.31%` reduction against independent sequential meshes. Compared
with the earlier `5,843,150`-byte EdgeBreaker independent baseline, the
topology-aware payload remains approximately `36.96%` smaller. Complete
OBJ-to-sequential-Draco authoring took about `1.10 s` for the sequence, while
encoding all 150 in-memory position point clouds took about `91 ms`.

The end-to-end authoring fixture generated an MP4 with five topology
keyframes and 145 position updates. It then reopened and verified all
150 packet payloads, timestamps, topology references, and a seek to frame 75.

The engine-neutral runtime now retains one bounded topology mesh per decoder
generation. Dependent point clouds replace its positions, reuse cached indices
and UVs, and reconstruct area-weighted normals. A complete compressed MP4
integration test presented 142 consecutive frames and recovered after a seek
to 2.5 seconds. The same synthetic H.264 fixture stops producing presentations
eight frames before EOS in both independent and compressed modes; that
pre-existing media/EOS issue remains tracked separately from temporal geometry.

The completed codec comparisons are retained above as a historical design
record. Their benchmark/test harnesses and optional dependencies have been
removed now that the implemented format uses Draco point clouds.

### Windowed benchmark dataset

The deterministic controlled sequence used during development exercises
topology-window detection, independent fallback, and packet-mode switching.
The current local dataset is stored under
`data/benchmark/windowed_topology` and contains matching `objs/` and
`texture/` sequences:

| Window | Source topology | Output frames | Length |
| ---: | --- | --- | ---: |
| 0 | `000110.obj` | `000000`-`000009` | 10 |
| 1 | `000111.obj` | `000010`-`000029` | 20 |
| 2 | `000112.obj` | `000030`-`000059` | 30 |
| 3 | `000113.obj` | `000060`-`000099` | 40 |
| 4 | `000114.obj` | `000100`-`000149` | 50 |

The first frame of each window is a byte-for-byte copy of its decoded source
OBJ. Dependent frames retain all non-position OBJ records and apply
deterministically seeded Gaussian noise to vertex positions. The standard
deviation is `0.0005` times the source bounding-box diagonal.

Each geometry window repeats the JPEG corresponding to its source topology.
All 150 textures are byte-for-byte source copies, use matching six-digit
frame names, and remain 1024x1024. This deliberately holds appearance
constant within a topology window while geometry changes, and permits the
complete Unity or Unreal authoring pipeline to encode the fixture. Audio is
optional; an existing test audio file can be supplied when audio
synchronization also needs coverage.

The analyzer verifies exactly five runs with lengths 10, 20, 30, 40, and 50,
145 reusable frames, no empty files, and 12,000 triangles in every frame. The
generated 150-frame dataset is approximately 178 MB and remains ignored by
Git; the generator source and validation logic are versioned.

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

### Phase 2: Unified packets

- Define and document independent and position-update headers.
- Add packet parsing and validation tests.

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

## Original design choices and implemented outcome

The initial plan proposed:

- exact canonical vertex and index correspondence;
- deterministically quantized UV fingerprints;
- one current packet format with explicit independent/update modes;
- complete Draco topology keyframes;
- keyframe-relative quantized integer position residuals;
- recalculated normals;
- a bounded maximum window/keyframe interval;
- Zstandard as the first residual-codec experiment; and
- automatic independent-Draco fallback whenever topology differs or reuse is
  not beneficial.

The completed comparison led to Draco point clouds for position updates
instead of a custom Zstandard residual payload. Consequently, LZ4 and
Zstandard are not dependencies of the implemented format.
