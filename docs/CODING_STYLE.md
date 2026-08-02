# Coding and API Conventions

These rules keep the native core and both engine integrations consistent.
They apply to new code and to existing code when it is materially changed.

## C++

- Use C++17, tabs for indentation, and braces on their own line.
- Use `snake_case` for functions, local variables, parameters, and namespace
  names; use `PascalCase` for types.
- Prefix private data members with `m_` and file-scope mutable state with
  `g_`. Prefer `nullptr` to `NULL`.
- Put engine-independent code in `openvolumetric`, reusable authoring code in
  `openvolumetric::authoring`, and Unity-native code in
  `openvolumetric::unity`.
- Public core headers must compile with core include paths alone. Do not expose
  Unity, Unreal, FFmpeg, Draco, graphics-resource, or concrete transport types
  through the core façade.
- Keep exported `extern "C"` ABI names in the global namespace with the
  `openvolumetric_` prefix. Engine-mandated callbacks may retain the engine's
  naming convention.
- Express ownership with values and smart pointers. Document every borrowed
  pointer or graphics handle with its owner, validity period, and permitted
  thread.

## C# and Unity

- Use `PascalCase` for types, properties, and methods, and `camelCase` for
  parameters and local variables.
- Existing private runtime fields use the `m_` prefix consistently. Serialized
  field renames must use `FormerlySerializedAs` to preserve scenes and
  prefabs.
- Keep native entry-point declarations private. Present a managed,
  engine-style API above them and make `Dispose` safe to call repeatedly.
- Unity object and graphics-resource work belongs on the main/render threads;
  audio callbacks must not mutate playback or graphics state.

## Unreal Engine

- Follow Unreal's `F`, `U`, `A`, `S`, and `E` type prefixes and its
  `PascalCase` member/function convention.
- Keep Unreal types inside the plug-in modules. The adapter may translate
  data, clocks, and ownership, but must not duplicate core decoding,
  synchronization, authoring, or adaptive-selection policy.

## Documentation

Comments explain API contracts, ownership, threading, invariants, and
non-obvious decisions. Avoid comments that merely repeat the next statement,
decorative divider comments, stale implementation narratives, and placeholder
notes. Public façade and ABI declarations should document success/failure
semantics and pointer lifetime.
