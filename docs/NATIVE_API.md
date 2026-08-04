# Native Runtime API

The Unity runtime boundary is a versioned C ABI declared in
`OpenVolumetricNative/integrations/unity/src/api/openvolumetric-unity-api.h`.
It deliberately contains no C++ references, STL types, exceptions, or
ABI-sized `long` values.

## Version and compatibility

The first stable contract is API version **1.0.0**.
`openvolumetric_get_api_version` returns the loaded library version in a
size-versioned caller-owned structure. Unity checks the major version before
creating a player.

- A different major version is incompatible and must not be used.
- A newer minor version may add functions, result values, or fields at the end
  of a structure without changing existing meanings.
- Patch versions fix implementation defects without changing the ABI.
- Callers set every `struct_size` to the size they compiled against. Native
  code rejects structures too small for the requested version.

## Handles and render events

`openvolumetric_player_create` allocates an opaque `OpenVolumetricPlayer`.
The caller owns it until exactly one successful
`openvolumetric_player_destroy`. Destruction is synchronized against active
API and render operations, but the caller must not begin another operation
with the same handle concurrently with destruction.

The opaque handle must never be interpreted, copied into serialized data, or
passed to `GL.IssuePluginEvent`. Unity obtains a separate integer through
`openvolumetric_player_get_render_event_id`; that value is only a registry
routing token valid for the lifetime of the handle.

## Values, strings, and buffers

- Input strings are borrowed UTF-8 and need remain valid only until the call
  returns. Native code copies any value it retains.
- Media, runtime, adaptive-switch, centroid, and adaptive-selection snapshots
  are caller-owned. Native code fills them synchronously and does not retain
  their addresses.
- Adaptive representation arrays are caller-owned. Every element has its own
  `struct_size`; insufficient capacity is reported explicitly.
- Detailed player errors are copied into a caller-owned buffer. The first call
  may use a null buffer to obtain the required capacity.
- Graphics handles remain owned by Unity. They must outlive playback and the
  player must be destroyed before Unity releases the registered resources.
- `GetRenderEventFunc` returns Unity's plug-in callback pointer; the plug-in
  owns that function for the lifetime of the loaded library.

No exported runtime function returns a borrowed string or metadata reference.
The former thread-local adaptive switch and manifest-selection getters were
removed during the version 1 migration.

## Native diagnostics

`openvolumetric_set_log_callback` installs one process-wide optional host log
sink. The callback receives a severity, a borrowed UTF-8 message, and the
opaque user context supplied during registration. The message is valid only
until the callback returns and must not be retained.

Diagnostics may originate on decoder, transport, authoring, or render
threads. A host callback must therefore be thread-safe, non-blocking, and must
not call player lifecycle or rendering functions. Registration is synchronized
with publication; an in-progress callback may finish while the host replaces
or clears the sink. Passing a null callback restores the platform fallback:
Android logcat, the Windows debugger/optional console, or standard error on
other desktop platforms.

The logger formats into fixed-capacity stack storage and invokes the host only
after releasing its internal lock. Real-time audio callbacks and recurring
per-frame decode paths do not publish diagnostics. Unity marshals native
messages into its console, Unreal routes them through `UE_LOG`, and native
tests may install an isolated capture sink without introducing engine types
into the core.

## Result model

Fallible calls return `OpenVolumetricResult`:

| Result | Meaning |
| --- | --- |
| `OK` | Operation completed successfully. |
| `INVALID_ARGUMENT` | Null, malformed, non-finite, undersized, or otherwise invalid input. |
| `INVALID_HANDLE` | The opaque player is null, destroyed, or unknown. |
| `UNSUPPORTED_FORMAT` | Required tracks/codecs or the active graphics backend are unsupported. |
| `CORRUPT_DATA` | Container, media, or geometry data is malformed. |
| `NETWORK_FAILURE` | Remote transport failed. |
| `TIMEOUT` | A bounded transport or decode operation timed out. |
| `CANCELLED` | Shutdown, seek, or caller cancellation stopped the operation. |
| `DECODER_FAILURE` | Media or geometry decoding failed. |
| `NOT_READY` | A valid value, such as a first geometry centroid, is not published yet. |
| `INTERNAL_FAILURE` | An invariant or platform operation failed without a more specific category. |

The category is suitable for program logic. The copied detailed error remains
the source for human diagnostics. New result values may be appended in a minor
release; callers must treat unknown values as internal failure.

## Snapshot consistency

Each snapshot is assembled during one shared instance access. Adaptive switch
state, generation, count, boundary, active/pending identifiers, and reason are
copied together instead of using a snapshot-then-get thread-local sequence.
Media metadata is copied by value while the instance is protected, so a later
close cannot invalidate a reference advertised to managed code.
