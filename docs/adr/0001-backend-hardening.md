# ADR 0001: M2.5 backend hardening boundaries

- Status: Accepted
- Date: 2026-08-14

## Context

M2 can render the same deterministic scene through preview and offscreen paths, but shader loading still embeds a build-tree
path, visual changes have no tolerant regression test, runtime failures are unclassified, and configuring CPU tests requires a
Vulkan SDK. M3 will change camera and tessellation behavior, so these gaps must be closed before those changes.

## Decision

- SPIR-V is a runtime asset owned by the executable. A renderer uses an explicitly supplied shader directory, or defaults to
  `assets/shaders` beside the executable. It never searches the source tree, build tree, or current working directory.
- OS-specific executable discovery is isolated behind a platform adapter. The scene and image layers remain platform-neutral.
- External/runtime failures use `AkariError` with a small stable category. Programmer contract violations continue to use the
  standard argument, range, and overflow exceptions.
- Image regression helpers and PNG decoding are test-only. A tracked 256x256 Unit Circle PNG is compared using alpha equality,
  an RGB threshold of 8, at most 1% threshold-exceeding pixels, and mean RGB error at most 1.0. Failed tests write artifacts only
  under the build tree.
- Golden images are updated only by an explicit `--update-golden` invocation and require visual review of the changed image.
- Renderer statistics observe uploads, growth, and pipeline creation without changing resource policy. Caching remains an M3.5
  concern after stable scene resource identities exist.
- `AKARI_ENABLE_VULKAN=OFF` removes Vulkan, VMA, shaders, applications, and GPU tests from the build while preserving core,
  image I/O, and CPU tests. Windows CPU-only CI uses this configuration; GPU validation remains a local/full-build requirement.

## Alternatives considered

- Embedding SPIR-V would remove file lookup but make shader iteration and future packaging less transparent.
- Searching the current or source directory would make binaries depend on invocation location and hide packaging errors.
- Byte-exact golden comparison is too sensitive to GPU rasterization differences; having no golden would leave M3 camera and
  tessellation changes unprotected.
- Implementing geometry and pipeline caches now would choose cache identities before the M3 scene model defines stable node and
  change-frequency semantics.

## Consequences

Applications must ship their `assets/shaders` directory. GPU tests require a real Vulkan 1.3 device and validation setup, while
CPU CI intentionally does not claim GPU coverage. Renderer statistics and Vulkan options remain experimental until the M3 API
review.
