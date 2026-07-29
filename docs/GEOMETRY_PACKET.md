# Geometry packet format

## Status

This document defines the sole geometry wire format selected after the
controlled Draco point-cloud benchmark. The core packet serializer and parser
implement this contract, and both authoring and runtime playback use it.

The numeric packet version is `2`; the earlier experimental 16-byte version 1
was removed before release and is not emitted or parsed.

## Coding modes

The format defines two packet modes:

| Mode | Value | Meaning |
| --- | ---: | --- |
| `IndependentMesh` | 0 | Complete independently decodable Draco mesh |
| `PositionUpdate` | 1 | Positions referencing a preceding topology keyframe |

The three authoring strategies do not require three wire modes:

- independent encoding emits only `IndependentMesh`;
- windowed encoding emits an `IndependentMesh` topology keyframe followed by
  `PositionUpdate` packets; and
- whole-sequence reuse is one window spanning the sequence, subject to forced
  periodic random-access keyframes.

This separation keeps the decoder concerned with packet dependencies rather
than authoring policy.

## Byte order

- Multi-byte integers use big-endian/network byte order.
- Reserved fields must be zero when authored and ignored when read.

All sizes are validated before allocation. Arithmetic used to derive decoded
sizes must be checked for overflow.

## Fixed header

The fixed header begins with the common magic and numeric format version:

| Offset | Size | Type | Field |
| ---: | ---: | --- | --- |
| 0 | 4 | bytes | Magic `VVGF` |
| 4 | 2 | `uint16` | Version (`2`) |
| 6 | 2 | `uint16` | Header size |
| 8 | 1 | `uint8` | Coding mode |
| 9 | 1 | `uint8` | Payload codec |
| 10 | 2 | `uint16` | Flags |
| 12 | 4 | `uint32` | Source frame number |
| 16 | 8 | `uint64` | Topology identifier |
| 24 | 4 | `uint32` | Referenced keyframe frame number |
| 28 | 4 | `uint32` | Vertex count |
| 32 | 4 | `uint32` | Triangle count |
| 36 | 4 | `uint32` | Encoded payload size |

The fixed header size is 40 bytes. `header_size` permits a
reader to skip understood future extensions. A reader must reject a header
smaller than the minimum required by its declared version/mode.

No quantization bounds are duplicated in this header. They are part of the
Draco payload and are interpreted by the linked Draco decoder.

## Flags

Defined flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `RandomAccess` | Packet can establish decoding without earlier media samples |
The format currently permits only bit 0. Unknown flags are rejected.

## Payload codecs

The selected representation uses Draco for both modes:

| Codec | Value | Intended use |
| --- | ---: | --- |
| `DracoMesh` | 0 | `IndependentMesh` payload |
| `DracoPointCloud` | 1 | `PositionUpdate` payload |

Only codec/mode combinations explicitly defined by the format are legal.
Codec identifiers are stable once published.

Position updates use Draco's sequential point-cloud encoding at the authored
position quantization. The sequential mode preserved canonical vertex order in
all 150 controlled test frames. KD-tree point-cloud encoding is not permitted
because it reordered points in every tested frame.

## Independent mesh payload

`IndependentMesh` contains a complete Draco mesh and:

- sets `RandomAccess`;
- provides the topology ID calculated from the canonical indices, winding,
  quantized UVs, and attribute layout;
- declares the decoded vertex and triangle counts;
- uses its own frame number as the referenced keyframe frame number.

## Position update payload

`PositionUpdate` contains a sequential Draco point cloud and:

- references a preceding `IndependentMesh` with the same topology ID;
- repeats vertex and triangle counts for validation;
- carries no indices or UVs;
- encodes exactly one three-component position attribute for every canonical
  render vertex;
- carries no indices, UVs, or normals; and
- relies on sequential Draco point order matching the cached canonical vertex
  order.

Each update is independently decodable once the referenced topology keyframe
is cached. It is not a delta from the preceding frame, so a dropped dependent
packet does not prevent decoding the next update.

## Topology identity

The topology ID is a deterministic 64-bit fingerprint over:

- canonicalization schema version;
- render-vertex count;
- triangle count;
- exact triangle index order and winding;
- UV presence;
- deterministically quantized UV values; and
- relevant attribute presence/layout.

Position and normal values are excluded. Hash equality is followed by an exact
canonical-data comparison during authoring. At runtime the ID selects a cache
entry, but packet counts and decoded-size checks must also agree before an
update is applied.

The current analysis implementation uses stable FNV-1a encoding over explicit
scalar bytes. A stronger identifier can replace it before version 2 ships if
collision experiments justify the extra dependency or cost.

## Random access and dependency rules

- A `PositionUpdate` may reference only the active preceding topology
  keyframe identified in its header.
- Authoring accepts a maximum geometry keyframe interval measured in geometry
  samples. A value of `N` permits at most `N - 1` dependent `PositionUpdate`
  packets after a full `IndependentMesh`; the next sample is forced to be a
  new full Draco reference mesh even when topology remains unchanged.
- An interval of `1` emits only independently decodable geometry. Disabling
  the optional limit passes `0`, allowing reuse until topology changes.
- Topology changes and explicit segment/random-access boundaries may force a
  reference mesh earlier than the configured interval.
- Seeking starts at or before the referenced topology keyframe.
- Adaptive or fragmented delivery repeats a topology keyframe at every
  independently addressable media-segment boundary.
- A missing or corrupt topology keyframe invalidates its dependent packets
  until the next random-access geometry packet.
- A corrupt dependent packet is dropped without invalidating later
  keyframe-relative updates.
- Seek, loop, close, and reopen operations advance the playback generation so
  cached topology from older asynchronous work cannot be reused.

## Normals

Dependent packets do not carry normals. The core reconstructs normals from the
decoded positions and cached triangle indices. Visual and angular-error tests
remain required before Milestone 11 exits.

## Parser validation

Parsing begins with the common magic and version:

```text
version == 2 -> parse and validate the fixed 40-byte header
otherwise    -> reject as unsupported
```

The packet and runtime layers accept the current format. Complete-mesh packets establish
the active topology cache; sequential Draco point clouds update its positions,
and the core reconstructs area-weighted vertex normals. Generation, topology
ID, keyframe number, vertex count, and triangle count must all match before an
update is applied.

## Remaining integration decisions

- Choose platform-preset defaults for the maximum geometry keyframe interval.
- Measure recalculated-normal error.
- Establish hard limits for vertex counts, payload sizes, and topology-cache
  memory.
