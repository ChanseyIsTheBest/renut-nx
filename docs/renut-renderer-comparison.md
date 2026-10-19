# Renut's native renderer against banjo-nx's (2026-10-16)

Renut upstream (`github.com/masterspike52/reNut`, commit `ce9be6b`, 2026-10-16; cloned to `D:\Banjo kazooie port\reNut`)
now renders Banjo-Kazooie: Nuts & Bolts at the Direct3D level through plume, on PC. banjo-nx started from Renut, and the
user decided (2026-10-16) to use its renderer for the Switch while keeping banjo-nx's own native renderer in the same
NRO, chosen at boot: it stays the exact reference and may stay better at some things. Credit: Renut by masterspike52
and contributors; its XenosRecomp fork (`thirdparty/reblue-XenosRecomp`) is MIT.

## What Renut's renderer is

Files: `src/renut_engine/gpu/` (about 1.2 MB of source).

- **Direct3D at the API level.** `Direct3D_CreateDevice`, the device's state calls (the render and sampler state tables
  N&B's `D3D::InitializeApiState` installs, about 60 render states including stencil and clip planes), the draws, Clear,
  Resolve, tiling, fences, Swap and the command buffer calls are native code ported from the game's own D3D
  (`d3d/d3d_native*.{h,cpp}`, `d3d/hooks/*.cpp`). The command ring is a stub that KickOff rewinds; no hooked call writes
  packets and nothing parses PM4.
- **State from the device.** The game's inline setters keep writing the device's shadows; a draw takes a snapshot of the
  device and the render thread records it (`backend/queued.h`, `render/draw.h`).
- **Recorded command buffers** (`guest/command_buffers.*`): a recording device's draws, clears, resolves and nested runs
  are kept with a copy of the device state they read and replayed on the render thread, with the tile predication mask.
- **Shaders:** XenosRecomp output compiled into the executable, looked up by container hash at Create*Shader (and for
  shaders registered in place by asset bundles, by object address); misses are captured to disk.
- **Targets** (`render/surfaces.*`): one host image per EDRAM view (base, pitch, samples, format, render scale), shared by
  every surface object over that view (N&B's holograms test against the scene depth through a second surface object). No
  content moves between views of different layouts.
- **Textures** (`render/textures.*`, `render/texture_layout.*`): found from fetch constants by guest address, untiled from
  guest memory; changes found by content hashes (tiled textures: a sampled fingerprint of the first and last 256 bytes and
  eight spans), lock-driven uploads for linear textures (the font cache), resolve destinations tracked by base address,
  CPU read-backs of targets (`target_readback_cs.hlsl`, photo mode in `guest/photo_capture.cpp`).
- **Geometry** (`render/draw.cpp`, `render/vertex_format.*`): vertex and index streams read from guest memory and
  converted (byte order, formats; `stream_convert_cs.hlsl`), with caches of converted copies re-hashed every few frames;
  memory export through export windows; tessellated water; point sprites and rectangle lists through geometry shaders.
- **Tiling:** replayed per tile; a draw predicated to one tile is cut to that tile's rectangle.
- **Extras beyond the console:** render resolution up to 4K, widescreen (Hor+), shadow and SSAO resolution, anisotropic
  filtering, an ImGui overlay; online play (EOS) and mods outside the renderer.

## What banjo-nx's renderer has that Renut's lacks

| Area | banjo-nx | Renut | Worth carrying over |
|---|---|---|---|
| Shader constants | uniform buffers, which NVK puts in Maxwell's constant banks: free operands in NAK (packages 19-25) | Vulkan: the three constant blocks through 64-bit pointers in push constants (`vk::RawBufferLoad`), global loads in NAK; D3D12: root CBVs | **yes, first**: the largest GPU item on the Switch |
| Shader translator | our XenosRecomp fork: 25 specialisation bits plus a second word, bool specialisation, branchless predicated blocks, early alpha, blend skip, bone rows, clamp bounds from constants, literal constants (packages 19-26; heavy pixel shaders 2,300-3,100 → 725-778 NAK instructions) | reblue-XenosRecomp: Unleashed's five specialisation bits, constants through pointers | **yes**: feed Renut's renderer our library |
| Pipeline compiles on the console | on-disk `VkPipelineCache`, prewarm list from the user's console, background compile threads, per-stage NVK cache | a list of pipelines built ahead once their shaders exist; the compiled code is left to the driver's own disk cache, which NVK on the Switch does not keep | **yes**: or every boot recompiles |
| Predicated tiling | tiles once: the scene drawn once at full size | each tile replayed, draws cut to the tile | **yes**: twice the scene's draw calls and vertex work otherwise |
| 2x MSAA scene | collapsed: one sample per pixel | real 2x / 4x multisample targets | **yes** for the Switch (GPU bandwidth); real MSAA stays a PC option |
| Recording threads | decoder, executor and a Vulkan recording worker | one render thread records everything | measure on the A57 first |
| Exactness | full fingerprints with verify modes for textures, vertices and indices; in-run A/B for every change | sampled texture fingerprints; stream and index caches re-hashed every few frames (a rewritten range can draw stale until the next check); no verify | **yes**: pixel-identical is the project's rule |
| Dead draws | draws that write nothing skipped (`banjo_gpu_skip_dead_draws`) | not modelled; occlusion queries not modelled either | yes |
| Switch platform | NVK fast paths, pipeline prefetch, thread cores and priorities, the nfsmw driver build, GPU timers, ARM-tuned hot paths | PC only (Windows / Linux, D3D12 / Vulkan) | yes, with Unleashed-NX's Switch plume |

## What Renut's renderer has that banjo-nx's lacks

- **No GPU emulation in the frame path:** the reason to take it (see `docs/rexglue-api-renderer-guide.md`).
- No EDRAM tile model, so no stencil rebuilds, imports or exports between targets of different layouts.
- Graphics options (resolution, widescreen, shadow and SSAO resolution, anisotropic filtering): PC options; on the
  Switch the user ruled out resolution and MSAA changes.

## Still to check at run time

Two Banjo cases banjo-nx had to fix were not found by name in Renut and are best checked by running both renderers over
the same route: the fur (a stacked texture streamed small levels first, with no base level) and the 2048x2048 `k_16_FLOAT`
shadow atlas resolved in strips.

## Status (2026-10-17, package 26)

Steps 1, 2 and 7 are done and part of step 3 (the uniform-buffer constants, branchless select and clamp bounds in
Renut's own translator; banjo-nx's shader library stays with banjo-nx's renderer): `docs/renut-renderer-port.md`. Step 4
is plume's persistent pipeline cache with Renut's own pipeline list. Steps 5 and 6 are not done (see that page,
section 5).

## Order of the port

1. Renut's renderer in banjo-nx as a second renderer chosen at boot (a toml key), over the SDK's detached mode (present in
   our SDK fork, `sdk/src/ui/rex_app.cpp`); the current renderer untouched.
2. Plume from UnleashedRecomp-NX (Vulkan only, its Switch patches).
3. Our shader library and the uniform-buffer constants path.
4. The console's pipeline cache and prewarm list.
5. Tiles once and the collapsed 2x MSAA scene.
6. Exact caches and verify modes where Renut samples.
7. Switch tuning, a PC verify against the current renderer, then a package.
