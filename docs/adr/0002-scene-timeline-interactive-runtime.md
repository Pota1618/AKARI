# ADR 0002: Dimension-neutral scene and interactive runtime

- Status: Accepted
- Date: 2026-08-14

## Context

M2 evaluates a bespoke 2D scene directly into triangle geometry. That path is deterministic, but it cannot represent stable
authored objects, non-destructive animation takes, recorded interaction, or stateful simulation. Making interactive and simulated
scenes separate scene types would duplicate timeline, preview, seek, and export semantics. Making the top-level model 2D-specific
would also force a disruptive redesign before 3D and 2D overlays can share a project.

ManimGL demonstrates a productive live scene workflow, but its mutable scene and render-frame updater model is not suitable as
AKARI's reproducibility boundary. SwapTube's explicit state dependencies are a useful model; AKARI needs equivalent separation
with typed properties, takes, input policy, and simulation state.

## Decision

- `SceneDefinition`, `SceneSession`, and `SceneSnapshot` are dimension-neutral. Spatial data is a tagged layer. M3 implements
  exactly one `Canvas2D`; it does not publish placeholder 3D types. A future milestone may add `World3D` and `Overlay2D` to the
  same scene/session after a separate ADR fixes 3D coordinates, camera, material, lighting, depth, and picking contracts.
- Transform parenting is confined to one spatial layer. Cross-layer relationships use typed properties or simulation inputs.
- Every authored node has an explicit stable key. `NodeId` is the fixed 64-bit FNV-1a hash of scene ID and stable key; zero is
  invalid, and duplicate keys or detected hash collisions fail scene construction.
- `PropertyHandle<T>` identifies a node property and admits only the registered `float`, `glm::vec2`, and `glm::vec4` kind/type
  pairs. A persistent property has one source: base value, active timeline take, or simulation output. A simulation output may
  not also have a timeline track. Live overrides are session-local and never mutate a finalized take implicitly.
- Timeline time is signed 64-bit nanoseconds. Keyframes are ordered and unique and support step, linear, and smoothstep
  interpolation. Recording forks the active take at the playhead, keeps the past and a continuity key, and replaces only the
  fork's future. Finalize activates the fork; cancel discards it. At an equal timestamp, the greatest semantic-event sequence wins.
- Scene definitions declare input sources and dimension-specific interaction bindings. GLFW events are converted after hit
  testing into `BeginDrag`, `Drag`, and `EndDrag`; platform events are not stored as scene data.
- Reproducibility is reported for a session range as `Replayable`, `Recording`, or `LiveOnly`, with the first affected time and
  blocking input sources. Starting a recording requires a replayable prefix. Clearing a transient override returns to canonical
  state rather than baking the current value.
- Simulation uses fixed ticks independent of render rate. The spring-mass demonstration uses semi-implicit Euler at 120 Hz and
  resets and replays from initial state for every arbitrary-time evaluation. An event at time `t` is assigned to
  `ceil(t * tick_rate)` and equal-tick events use sequence order. Checkpoints and baking are later optimizations, never correctness
  requirements.
- `LayerSnapshot2D` is tessellated through the existing `SceneFrame2D` renderer boundary. Preview and offscreen rendering keep the
  same evaluated snapshot, camera, tessellator, shaders, and draw pass.

## Alternatives considered

- Separate timeline, interactive, and simulation scene classes would make composition difficult and create divergent seek and
  export rules.
- A universal transform/camera abstraction would either encode only the lowest common denominator or prematurely freeze 3D
  conventions.
- Frame-delta updaters are convenient for live playback but make backward/random seek and offline reproduction depend on the
  previously rendered frame.
- Baking every drag directly into base properties would destroy prior animation and make record/edit intent ambiguous.

## Consequences

Arbitrary-time simulation is currently proportional to elapsed ticks; M3.5 may add checkpoints keyed by stable IDs. Geometry
revision and static/dynamic hints are carried in snapshots but do not yet drive a GPU cache. Scene/take serialization, curve
simplification, simulation baking, multiple layers, and all 3D contracts remain deliberately unresolved. The M3 types are
experimental internal interfaces and do not establish a stable ABI.
