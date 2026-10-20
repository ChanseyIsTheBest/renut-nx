# Renut's renderer in banjo-nx: the port (2026-10-16/17)

banjo-nx now carries two renderers in one NRO, chosen at boot:

| `banjo_renderer` | Renderer | What it is |
|---|---|---|
| `native` (default) | banjo-nx's own | The PM4/EDRAM renderer (masseffect-nx derived) with every `banjo_*` GPU and ring key. The exact reference. |
| `renut` | Renut's | Renut upstream's Direct3D-level renderer (`github.com/masterspike52/reNut`, commit `ce9be6b`): N&B's Direct3D calls run natively through plume (Vulkan; D3D12 on Windows too). No PM4, no command ring, no EDRAM model. |

Credit: Renut by masterspike52 and contributors (the renderer has no licence file; the user approved its use, banjo-nx
started from Renut). Its XenosRecomp fork (`thirdparty/reblue-XenosRecomp`) is MIT, plume is MIT, and the Switch backend
of plume and volk comes from UnleashedRecomp-NX's patches. What the two renderers are, side by side:
`docs/renut-renderer-comparison.md`. How any ReXGlue port can go the same way: `docs/rexglue-api-renderer-guide.md`.

This page is what was done to bring Renut's renderer into banjo-nx, onto the Switch, and towards the A57 and NVK, with the
keys, the files and what was checked.

## 1. How it is built in

### The import (`tools/renut_gpu_import.py`)

`python tools/renut_gpu_import.py --renut "D:/Banjo kazooie port/reNut"` copies Renut's `src/renut_engine/gpu` into
`app/src/renut_engine/gpu` and rewrites what differs in banjo-nx:

- **Hooks by address.** Renut names its hooks (`REX_HOOK(D3DDevice_Clear, ...)`, names from its
  `config/renut_gpu_funcs.toml`); banjo-nx's generated code names those functions `sub_<address>`. Every hook (361)
  becomes `BANJO_RENUT_HOOK(sub_X, fn)` / `BANJO_RENUT_HOOK_RAW(sub_X)`: Renut's function when Renut's renderer is chosen,
  the game's original otherwise (`banjo_renut_select.h`).
- **Shared addresses.** 34 addresses are hooked by banjo-nx too (its renderer's hooks, the record writers). There Renut's
  function becomes `renut_native_sub_X`, and banjo-nx's hook calls it first when Renut's renderer is chosen
  (`BANJO_RENUT_YIELD_SHARED`). The list: `banjo_shared_hooks.h` and `app/src/me/banjo_renut_shared.txt` (which
  `tools/d3d/d3d_record_hooks.py` reads). Checked: every one of the 34 has its hand-over.
- **Imports** of game functions Renut names (`XMemFree`) become banjo-nx's names; `perf/frame_stats.h` is copied
  (banjo-nx has its own hook-free `frame_stats.cpp`).
- **gpu.cmake:** Renut's XenosRecomp ExternalProject gets the whole host toolchain (`BANJO_RENUT_XENOS_RECOMP_ARGS`), the
  host DXC is chosen by `CMAKE_HOST_WIN32`, no X11 on the Switch, and a hook lets `cmake/banjo_renut_gpu.cmake` supply the
  shader cache (the Switch build).
- **banjo-nx's changes to Renut's files** are one patch, `tools/renut_patches/banjo.patch`, applied after the rewrite
  (`git apply` from the repository root with `--directory`: inside a work tree git silently skips paths otherwise). After
  editing the imported files: `python tools/renut_gpu_import.py --renut ... --save-patch` (it diffs the tree against a
  fresh rewrite of the same checkout). banjo-nx's own files in that folder are kept by name: `banjo_renut_select.*`,
  `banjo_shared_hooks.h`, `banjo_renut_capture.h`, `banjo_renut_tuning.*`. `--check-into <empty folder outside the
  repository>` writes a fresh import with the patch applied there and compares it with the tree: "round trip OK" means
  a Renut update can be imported the same way (checked 2026-10-17). Keep Renut's files in CRLF, as Renut has them:
  an edit that changes a file's line endings makes the patch rewrite every line of it.

It is an OBJECT library (`banjo_renut_gpu`), not a static one: the hooks replace the generated code's weak `sub_X`
definitions, and a static archive's members are only pulled in for undefined symbols. `BANJO_RENUT_RENDERER=0` builds
without it.

### The app

`renut_app.h`: with `banjo_renderer = renut`, `OnPreSetup` still applies the Switch settings (`banjo_mesa_disk_cache_off`,
`banjo_gpu_boost`, ...), then sets no SDK graphics system and no GPU plugin (`renut::gpu::ConfigureRuntime`); the renderer
starts from `OnFinalizePaths` and gets the SDK's ImGui drawer for the menus. The CPU side (codegen rounds, hot functions,
hand-written natives, critical sections, the log writer, reverb) is the game code's and runs the same with either
renderer.

### Shaders

Renut's renderer takes its shaders from Renut's XenosRecomp, compiled into the executable as a zstd'd cache (SPIR-V
smol-v'd, DXIL on Windows), keyed by the XXH3 of the guest container:

- Input: Renut's own dump of `default.xex` and the bundles (`reNut/tools/dump_shaders.py` → `out/renut_shader_dump`,
  4,731 containers; banjo-nx's harvest has wrapped containers Renut's translator does not take), plus the shaders a run
  captured because the cache lacked them (`app/out/pc-me/shader_capture`, 150 on the intro route: shaders the game makes
  itself). Game data: never committed or shipped apart from the NRO.
- The capture directory had never been collected: Renut's `set(RENUT_SHADER_CAPTURE_DIR ... CACHE PATH "a" "b")` has two
  docstring arguments, which makes `set` a plain list; fixed (one docstring).
- **Switch:** XenosRecomp is a host tool. The PC build generates `app/out/pc-me/renut_gpu/shader_cache.cpp`;
  `python tools/renut_switch_shader_cache.py` writes `app/out/renut_switch/shader_cache_switch.cpp` with the DXIL arrays
  emptied and the compressed SPIR-V in `spirv_cache.zst` beside it, embedded by the assembler (`.incbin`: GCC needed
  over 15 minutes and 1.8 GB for the 4 MB initializer list), about 1 MB of source; the Switch build compiles it
  (`BANJO_RENUT_SHADER_CACHE_CPP`). **Build the PC tree first after any translator change**, then run the script.

## 2. Fixes to run it

| Change | Why |
|---|---|
| A window that starts minimized (`window_start_minimized`, the automated tests) no longer fails the renderer: the swapchain's targets are built once the window has a size | plume's Vulkan swapchain is empty at 0x0; Renut treated that as fatal at start (it already handled minimizing later) |
| plume's VMA under its own names (`plume_vma_names.h`) | the SDK links another VMA (3.3.0 against plume's 3.2.1): `lld-link: vmaGetHeapBudgets was replaced` on Windows, duplicate definitions on the Switch |
| plume `copyTextureRegion` from a texture into a buffer: added to the Vulkan backend; the D3D12 backend no longer sets sample positions on the (missing) destination texture | the capture below uses it; both backends crashed on it |
| Frame rate in the log for both renderers: `[frames] N game frames in 10.0 s: X fps (Renut's renderer)` | one figure to compare the two by (D3DDevice_Swap) |
| Test captures (`banjo_renut_capture.h`): `nfsmw_captura_cada_s`, `banjo_video_s` and `banjo_record_s` work with Renut's renderer | they read the SDK presenter, which Renut's renderer does not use; the next Present scans the frame out once more into an offscreen target and reads it back (a GPU wait per capture: a test tool) |

## 3. The Switch

| Part | What |
|---|---|
| plume | Renut needs plume `d723793`; UnleashedRecomp-NX's patches (`plume.patch`: the Switch platform with `VK_NN_vi_surface` on libnx's `NWindow`; `plume-switch-perf.patch`: precise barriers, the persistent pipeline cache, dynamic constant buffers, the device-lost and driver-message callbacks, ...) are written against `1192686`. They were merged three-way onto `d723793` (conflicts in the swapchain description and `_WIN32` blocks resolved; LSFG-VK frame generation is stubbed: `plume_lsfg_stub.cpp`). |
| volk | UnleashedRecomp-NX's `volk.patch` (no `dlfcn.h` on the Switch; `volkInitializeCustom` with the static NVK's `vk_icdGetInstanceProcAddr`). |
| volk's names | `plume_volk_names.h` (Switch only): volk's 759 function pointers are `plume_vk*`. Mesa's static NVK exports the Vulkan entry points as functions and the SDK calls them directly (`vulkan_instance.cpp`); volk's globals of the same names would define each a second time. |
| NVK | plume links the same static NVK as the SDK (`REXGLUE_SWITCH_NVK_SDK`), `PLUME_PLATFORM_SWITCH` (`cmake/banjo_renut_gpu.cmake`). Since 2026-10-20 (package 36m) neither links it unless `REXGLUE_SWITCH_NVK` is ON (`PLUME_SWITCH_NVK_LINK` follows it); the default Switch build is deko3d only (docs/deko3d-port-faq.md). |
| window | `backend/native_window.cpp`: libnx's default `NWindow`. |
| threads | `std::thread::detach` throws on the Switch (package 24's crash): Renut's prebuild workers detached themselves. They and the render thread are now threads that are never joined, made with pthreads and explicit stacks on the Switch (render 1 MB, workers 512 KB; `StartLongLivedThread`), std::threads elsewhere. |
| files | `pipeline_list.bin` (Renut's list of the pipelines to build ahead), `shader_capture/` (shaders the cache lacked) and `cache/renut_pipelines.bin` are next to the executable (the NRO's folder), not in the working directory. |
| Dear ImGui | one copy on the Switch (the SDK's, same 1.92.5): Renut compiles its own elsewhere to keep its overlay's context away from the SDK's, which a static link would define twice. Renut's own overlay is not started on the Switch; the SDK's menus are drawn. |

## 4. Towards the A57 and NVK

Every item has its key in the enhancement table (`src/cpu/banjo_enhancements.cpp`, the `[enhancements]` log line) and
goes off with its group's master switch.

| Key | Group | Default | What | Same output? |
|---|---|---|---|---|
| (build) uniform-buffer constants | translator + renderer | on (Vulkan) | The guest shaders read their float constants and SharedConstants from three dynamic uniform buffers (sets 5, 6, 7; one descriptor set per upload-ring chunk, bound at the allocation's offset, rebinds skipped when unchanged) instead of `vk::RawBufferLoad` through the push constant's 64-bit addresses. NAK compiles a raw load to a global memory load; NVK keeps dynamic UBOs in Maxwell's constant banks, where a constant is a free operand (banjo-nx's renderer: MASSEFFECT_UBO; UnleashedRecomp-NX: SPEC_CONSTANT_CONSTANTS_UBO). | Yes: the same bytes at the same offsets. PC run of the intro route: same frames. |
| (build) branchless select | translator | on | The scalar predicate updates and the kill tests are `select()` instead of HLSL 2021's short-circuit `?:`, which DXC writes as branches NAK keeps (`banjo_gpu_branchless_select` of banjo-nx's translator: PS536 54 → 37 branches). | Yes: no side effects in the operands. |
| (build) predicated blocks without a branch | translator | on | Renut's translator wraps nearly every predicated instruction and exec block in `if (p0)` (190-390 a heavy pixel shader). A pass over its output (`banjo_pred_select.h`) turns a block of plain register arithmetic (targets rN, ps, p0; no fetch, load, kill, output, address register, derivative or indexing; at most 12 statements; not one with an `else`) into `lhs = select(pb, (type)(rhs), lhs)` with the predicate taken at the block's entry, nested blocks under `and(outer, p0)`. banjo-nx's `banjo_gpu_pred_select` / `_ps`, rewritten for this translator. 244,644 blocks over the 4,881 shaders; the four largest pixel shaders: conditional branches 231 -> 80, 415 -> 93, SPIR-V instructions -17 to -20 %; compressed SPIR-V 3.87 -> 3.59 MB. Build-time: `BANJO_RENUT_PRED_SELECT=0` in the build's environment turns it off. | Yes: each statement runs in order when the predicate holds and each target keeps its value when it does not; the targets are temporaries, the right sides have no side effects. Pictures of the route unchanged. |
| (build) branchless vertex fetch swap | translator | on | `vfetchSwap` (the endian swap of every word a vertex fetch from memory reads: bone palettes, instance data) chose its swap with an if-chain on the draw's endian mode, a branch per word; now the three swaps and `select()`. Skinning vertex shaders (12 bone fetches): conditional branches 178 -> 25 (with the predicated blocks), SPIR-V -32 %; compressed SPIR-V 3.59 -> 3.43 MB. | Yes: the same word for every mode (checked over 800,000 random words). |
| (build) branchless helpers | translator | on | The alpha test picks its comparison with `select()` instead of a `switch` on the draw's function (an `OpSwitch` per test, which NAK lowers to a compare-and-branch chain: a small alpha-tested pixel shader had 4); `rcpf`/`rsqf` and the texture scale decode use `select()` instead of `?:`. Compressed SPIR-V 3.43 -> 3.30 MB. | Yes: the same comparisons and values, NaN included. |
| (build) clamp bounds from the constant bank | translator + renderer | on (SPIR-V) | The Xenos RCP / RSQ / LOG clamps take FLT_MAX from SharedConstants' word at 1340 (FLT_MIN is its negation) instead of 32-bit immediates NAK moves into a register before every clamp (`banjo_gpu_clamp_cbuf`: 12-27 moves in each heavy pixel shader). | Yes: the same value. |
| `banjo_ring_core` | (parameter) | 1 | Renut's render thread (the job banjo-nx's ring thread does) pinned to core 1, priority 0x2C. Renut started it as a plain `std::thread`, which libnx puts on the process's default core with the game. | Yes: only where it runs. |
| `banjo_background_compile_core`, `banjo_prewarm_threads` | (parameters) | 2, 1 | Renut's pipeline prebuild workers and its content verifiers (the threads that re-hash cached vertex and index copies): on core 2, priority 0x3F; one prebuild worker (Renut starts half the cores'). | Yes. |
| `banjo_renut_pipeline_cache` | non-CPU | `true` | The driver's compiled pipelines persist in `cache/renut_pipelines.bin` (plume's VkPipelineCache), so Renut's prebuild of its pipeline list (`pipeline_list.bin`) loads them instead of compiling. Saved on a low-priority thread when something new was compiled (at most once a minute; on the Switch a save asks the driver for nothing when every creation hit) and at exit. The key is the shader package (a new package starts the file over). | Yes: the same pipelines. |
| `banjo_renut_linked_stages` | non-CPU | `true` (Switch) | With Renut's renderer the app does not set `NVK_SWITCH_STAGE_CACHE` (`masseffect_nvk_cache_per_stage`, on for banjo-nx's renderer), which makes NVK compile each stage without the other (`disable_lto`). Renut's vertex shaders write every output (zeros for the unused ones, about 20 vec4 a vertex); only a linked compile drops the ones the pixel shader does not read. More compiling, which `banjo_renut_pipeline_cache` keeps. | Yes: unread outputs. |
| `banjo_renut_precise_barriers` | non-CPU | `true` | plume's barriers take their access masks from the layouts and declared accesses instead of MEMORY_READ \| MEMORY_WRITE, which NVK turns into cache flushes and invalidations (UnleashedRecomp-NX's SwitchPreciseBarriers). | Yes: the same ordering for every access that exists. |
| `banjo_renut_upload_cached` | CPU | `true` (Switch) | The upload ring (constants, converted vertices and indices, texture uploads) in CPU-cached memory, published with a flush of what was written before each submission (plume: `RenderBufferFlag::HOST_CACHED`, `unmap(0, &writtenRange)`), instead of write-combined memory: 5,251 MB/s against 2,506 on the A57 (`masseffect_native_upload_memory = 1`). PC: write-combined (cached memory is slower for its discrete GPU to read). | Yes: the same bytes reach the GPU. |
| `banjo_renut_push_point_only` | CPU | `true` | With the constants in uniform buffers only the point sprites' geometry shader reads the draw's push constant (the three constant addresses): only point-list draws push it (NVK: a root-table upload a draw less; banjo-nx's `banjo_gpu_push_once` is the same idea). | Yes: no other shader reads it. |
| `banjo_renut_capture_pool` | CPU | `true` | A queued draw's capture (its 12.7 KB device snapshot, the copied vertices and indices, the closure the render thread runs) comes from a pool and goes back to it once drawn, instead of three heap allocations per draw on the game thread freed on the render thread (about 2,000 a frame through the Switch's single malloc lock); the closure holds a plain pointer, which `std::function` stores without allocating. PC, same route: the frame rate up in most 10 s windows. | Yes: the same data. |
| `banjo_renut_shared_dedupe` | CPU | `true` | A draw whose SharedConstants (1,344 bytes: texture and sampler slots, booleans, render state) equal the previous draw's on the same command list reuses that upload: no copy into upload memory and no set-7 bind (NVK: a root-table update). PC, intro route: 20-28 % of about 1,500 draws a frame; the `[renut]` line counts them. | Yes: exact compare of the bytes. |
| `banjo_renut_queue_notify_idle` | CPU | `true` | Work queued for the render thread (every draw) wakes it only while it waits in its condition variable, instead of a notify per item: on the Switch every notify is a kernel call, about 1,500 a frame from the game threads, most while the render thread is busy. The render thread reads the queue under the same lock before it sleeps, so no push is missed (the same rule as the SDK's `thread_signal_skip_idle`). | Yes: timing only. |
| `banjo_renut_neon_swap` | CPU | `true` (ARM) | The vertex conversion's byte swaps (guest big-endian words to the host's) 16 bytes per NEON `rev16`/`rev32` instead of a word at a time; four-word elements are one vector. Checked against Renut's `SwapDword` through simde: `tools/tests/renut_neon_swap_test.cpp`, 600,000 random vectors, 0 mismatches. | Yes: the same bytes. |
| `banjo_renut_pmull_hash` | CPU | `true` (ARM) | Renut's per-frame content hashes of guest data (vertex streams, index ranges, texture samples, CPU-written textures) use banjo-nx's PMULL-folded CRC-64 (`banjo_pmull_fingerprint.h`) instead of XXH3: half the instructions per byte on the A57. A self-test against the bit-by-bit reference at start (any difference keeps XXH3). The shader containers keep XXH3 (the cache is keyed by it). | Yes: the hashes only compare with themselves; 2^-64 odds. |

## 5. What is not carried over, and why

Most of `docs/enhancements.md` is about the work Renut's renderer does not do at all: the PM4 ring and its decoder, the
Direct3D records, EDRAM tiles and their imports, the stencil passes, the tile replays, the ring's vertex arenas and
dedupes. Those costs do not exist there. Of the rest:

- **banjo-nx's translator rounds** (pred select, vfetch block, bone padding, early alpha, blend skip, structured CF, the
  per-slot texture signs, the pruned and unwritten vertex outputs, tess grid, literals in registers...) are written for
  banjo-nx's translator (masseffect-nx's XenosRecomp fork) and its renderer's specialization constants. Renut's renderer
  uses Renut's translator; the uniform-buffer constants, branchless select, clamp bounds and the predicated blocks
  without a branch were carried over (above). Each of the others needs its own port to that translator and a check;
  candidates in order of measured value: `banjo_gpu_bone_padding`. What it removed in banjo-nx's translator (the
  compare and select per word of every indexed fetch, 48 a vertex in the skinning shaders) does not exist here:
  `vfetchMemory` is a raw buffer-address load without a bounds check. What remains is the predicated branch around
  each bone fetch, which stays: when the predicate is false the index register can hold anything, and an unguarded
  load at a wild address is a GPU memory fault (on the console, a lost device). Removing it needs the copy's size in
  the shared constants and a clamp of the address, then padding past the end; not done. `banjo_gpu_vs_outputs_unwritten` is left to the driver here (`banjo_renut_linked_stages`). (A few
  memexport vertex shaders keep 1,600-1,700 branches in their 49 stacked 3D texture fetches.) The texel-offset term
  needs nothing: DXC already folds `0/size` for the zero offsets most fetches have (checked: the same texture size
  queries with or without a guard).
- **The codegen improvements** (generated game code) are the game's and apply with either renderer.
- **Collapsed MSAA**: Renut creates real multisampled targets where the game asks for them (the 2x scene), where
  banjo-nx's renderer draws one sample per pixel on the console. Renut's default is left as Renut has it (no MSAA or
  resolution changes without the user's say);
  `configs/banjo_renut_nomsaa.toml` sets the game-level `disable_msaa`, which gives Renut's renderer that same
  one-sample image (checked on the PC over the intro route) for the console to compare.
- **Tiles once**: nothing to port; Renut already draws a tiled scene once. A surface bound between `BeginTiling` and
  `EndTiling` is backed by a target covering every tile, the scene's draws run once into it, and D3D's replay of the
  tiles' ring segment per tile (its WorkerDpc callback) is left out (`d3d/hooks/draw.cpp`). What stays per tile is what
  the game itself issues per tile (draws predicated to one tile, cut to its rect: the vehicle parts' per-tile command
  buffers, the water with each tile's constants) and `EndTiling`'s resolve of each tile's rect: the console's work, not
  emulation. (Package 26's version of this page said Renut replayed per tile: wrong, corrected 2026-10-17.)

## 6. Checking it

What was checked on the PC (RTX 3060 laptop, Vulkan, the console's `renut.toml`, the intro route into Spiral Mountain):
every step's pictures (contact sheets of captures every 15-30 s: logo, title, menus, the intro cutscenes, the hub,
gameplay with Banjo, Kazooie and the HUD); 300 s runs with the 30 fps cap holding 30 throughout; uncapped, Renut's
renderer ran about twice our renderer's frame rate on the same route (about 100 against 50 fps; the PC is not the
console). banjo-nx's tuning keys all off against all on (the shader changes stay, being in the build): 15-20 % fewer
frames in most 10 s windows of the same route (for example 180 against 219, 143 against 171, 114 against 136 fps),
the pictures the same. Renut's renderer on D3D12 (its Windows default) over the same route: the same pictures (the
translator changes reach its DXIL too). Our renderer in the verify mode over the same route: 0 differences. A 600 s run of the final build (03:52): 30.00 fps in
every 10 s window but two in the intro's loading (28.6, 29.5); its memory report settles once the hub has loaded (upload
rings back to 64 MB, stream cache 24 MB in a 128 MB pool, command buffers 11 MB in 36) and stays there for the last
five minutes: nothing grows. The five in-run A/Bs on the PC (240 s each, the 30 fps cap, the 20 windows after the
loading; means of each half):

| Target | Render thread on / off (ms a frame) | GPU on / off (ms a frame) | Reading |
| --- | --- | --- | --- |
| 1 precise barriers | 5.47 / 5.40 (+1 %) | 10.35 / 11.58 (**-10.6 %**, lower in 17 of 20) | a real GPU saving |
| 2 render thread pin | 5.53 / 5.48 | 10.27 / 10.29 | noise on the PC (Windows schedules its own cores) |
| 3 SharedConstants reuse | 5.50 / 5.53 | 10.19 / 10.47 (-2.6 %) | within the noise |
| 4 push constant skip | 5.56 / 5.42 | 10.24 / 10.43 | within the noise |
| 5 capture pool | 5.61 / 5.38 (+4 %, lower in 10 of 20) | 10.34 / 10.29 | within the noise |

Targets 2-5 save kernel calls, allocations and copies that cost little on the PC's CPU (its render thread idles more
than 80 % of a frame at the cap); the console's A/B runs are the ones that judge them. The Switch NRO links with both renderers;
nothing of Renut's renderer has run on a console yet.

- PC: `bash run_console.sh <name> 150 --banjo_renderer=renut --renut_gpu_backend=vulkan --renut_gpu_profile=10
  --nfsmw_captura_cada_s=15` (the console's settings and the intro route), then a contact sheet of `capturas/`.
- In-run A/B (`banjo_renut_ab_target`, half-period `banjo_renut_ab_s`, the `[renut A/B]` line every 10 s): 1 precise
  barriers, 2 the render thread's pin, 3 the SharedConstants reuse, 4 the push constant skip, 5 the capture pool
  (`configs/banjo_renut_*_ab.toml`). The other keys change memory types or hashes at start: whole-run configs.
- Console: `configs/banjo_renderer_renut.toml` (see the package README). Renut's configs set
  `renut_post_process_precision = "Game"`: the bloom, depth-of-field and SSAO buffers in the console's 8-bit format
  (Renut's default "High" makes them 16-bit: twice their bandwidth on the Switch, and an image that differs from the
  console's and from banjo-nx's renderer); `configs/banjo_renut_post_high.toml` is Renut's default. Renut's other
  graphics options default to the console's (`renut_resolution = "720p"`, shadows following it, half-resolution SSAO,
  the game's anisotropy). In the log: `[enhancements]`, `[renut]`
  (thread placement, pipeline cache, content hashes), `[frames]` every 10 s, `GPU profile` with `renut_gpu_profile`.

## 7. Package 27 (2026-10-17): the rest of the port

What package 26 left (section 5's candidates, the exactness of Renut's caches, the console's memory) and every shader
translator optimisation of banjo-nx's that applies to Renut's translator. Each item has its key (most are banjo-nx's
renderer's own keys, which Renut's renderer now reads too), and where it can be switched while running, an in-run A/B
target of `banjo_renut_ab_target`.

### Exact and safe on the console (not enhancements: no master switch)

| Key | Default | What | Checked |
|---|---|---|---|
| `banjo_renut_exact_streams` | `true` | Renut keeps converted copies of vertex streams and index ranges whose guest bytes held for a few flushes, and checked them on a background thread once every 4 frames: a range the game rewrote could draw from its old copy for about 5 frames. Now a copy is hashed whole on its first use after each kick and converted again if its bytes changed: Renut completes a kick's fence as soon as the render thread has read the kick's data, so after any kick the game may know the GPU is done with a buffer and write it again (also after memexport results land in guest memory). What it does not cover, like Renut's own check: a write the game makes after the draw call and before the kick (Direct3D allows it; Renut reads uncached streams at the kick, but a cached copy is chosen and hashed when the render thread takes the draw, which can be earlier). In-run A/B target 6. | PC route: 450,000-505,000 checks of 5.4-7.1 GB every 10 s (18-24 MB a frame), 0 stale copies caught in any run, and Renut's own check (key off, `configs/banjo_renut_exact_off.toml`: the `[renut]` line counts the copies it finds late) found none either. Cost, PC in-run A/B (17 windows after the loading): render thread 5.00 -> 6.47 ms a frame (+1.47 ms, dearer in every window). On the A57 the same bytes at about 3 GB/s (the PMULL hash) would be 6-8 ms a frame: read the console's A/B before judging Renut's renderer's speed. A cheaper rule that stays exact (hash only when the game can have seen a fence since the copy's last use) needs the fence count carried with each queued draw, recorded command buffers included: not done. |
| `banjo_renut_memory_policy` | `true` on the Switch | Renut's limits were sized for a PC's video memory; NVK on the console gives about 1,280 MB of budget in all. Cold host textures (not bound for 120 frames) are released when less than `banjo_renut_textures_reserve_mb` (192) of the budget is left or the bindless table is three quarters full, checked every 32 frames; the stream cache stops at `banjo_renut_stream_cache_mb` (128; Renut 768) and drops copies idle `banjo_renut_stream_cache_idle_s` (20 s; Renut 120); upload ring chunks a heavy frame added go after `banjo_renut_upload_trim_frames` (120; Renut 600). A released texture is made again from guest memory when bound. Rules from sa2-nx's console experience (its glitches came from too little eviction and stale resolve memory, not from evicting): only cold textures, never one the GPU wrote, budget measured by the driver. | Forced-eviction run (`configs/banjo_renut_eviction_test.toml`: a reserve larger than any budget, released slots poisoned magenta): no magenta in any capture, the same pictures. |
| `banjo_renut_keep_gpu_written` | `true` | A host texture the GPU wrote (a resolve destination; Renut writes nothing back to guest memory) is never released by an eviction rule: made again from guest memory it would hold whatever that memory holds. Renut's own backstop released them after 3,600 idle frames. | |
| `banjo_renut_diag_poison_slots` | `false` | A test: descriptor slots given back point at a magenta texture instead of the null one (sa2-nx's poison test). | |
| plume: `VK_EXT_memory_budget` | always | VMA's heap budgets and usage from the driver (every allocation's real footprint) instead of its estimate (80 % of each heap, its own allocations only): the reserve above and Renut's backstop read the real budget. | PC: budget 5,230 MB on the laptop's GPU, as the driver reports it. |
| plume: pipeline cache save | always | Its buffer allocated without throwing: a save that finds no memory is skipped (banjo-nx's own renderer once ended the process that way on the console). | |
| memory report | always | On the Switch a `Memory: heap` line: the malloc heap in use, its size (1,024 MB, half of it the guest's memory) and its free pieces. | |

### GPU and driver (banjo-nx's renderer's keys, now read by Renut's renderer too)

| Key | Where | What | Measured / checked |
|---|---|---|---|
| `banjo_gpu_nvk_draw_contract` (+ `masseffect_native_nvk_emission`, `_cbufs`, `_dynamic`, `_preload`, `masseffect_native_set4_differences`) | Switch | NVK's per-draw improvements (each draw's commands in one go, only the dirty cbuf slots and dynamic state groups, the pipeline prefetched) and dynamic descriptors by differences (Renut re-binds sets 5-7 with new offsets every draw: only the words that change go to the root table) are process-wide requests in the linked NVK (`nvk_switch_dibujo`, `nvk_switch_set4` in mesa-switch-main-nfsmw-source), each exact and checked by its own guard. Banjo-nx's renderer made them; with Renut's renderer nobody did. A `[renut] NVK parts` / `NVK improvements` line every 10 s gives NVK's own times and each guard's state. | console |
| `banjo_gpu_robustness_off` | Vulkan | Renut's pipelines without buffer and image robustness (`VK_EXT_pipeline_robustness`; plume now offers it): NVK drops its two descriptor index clamps from every texture handle. Renut's indices are in range by construction. | PC log: "off for the game's pipelines" |
| `banjo_gpu_bool_spec` | Vulkan | See the translator table: the renderer side is the pipeline key's bool words and the generic pipeline while a specialised one is built by a prebuild worker. In-run A/B target 7. | PC route: about 110,000 of 226,000 draws a 10 s window draw with specialised bools; new combinations draw with the generic pipeline for a few frames (3-292 draws in a window while building) |
| `banjo_gpu_blend_skip` | Vulkan | See the translator table; the mode comes from the pipeline key (one UNORM target, no depth or stencil write, a blend that keeps the destination for a zero source). Renut has no occlusion queries, so a discarded pixel changes no count. In-run A/B target 8. | |
| `banjo_gpu_tess_grid` | all | Tessellated draws indexed by grid vertex: the vertex shader runs once per grid vertex instead of once per triangle corner. The host's index list (one per kind and level, made once) names the same triangles in the same order with the same corners, and each grid vertex gets the same weights. In-run A/B target 9. | PC route: 3.0 million grid vertices for 15.2 million corners every 10 s (the water) |

### Renut's translator: banjo-nx's translator optimisations, ported

Counts over the library (4,881 shaders: 3,936 pixel, 945 vertex). Build-time switches are environment variables of the
build (the shader cache is rebuilt), or the CMake option named.

| Change | What | Count | Off |
|---|---|---|---|
| Moves | the Xenos move (MAX a, a) written as `a` (banjo-nx's translator since its first rounds); exact, NaN and both zeros included | 38,482 | (always) |
| Bool constants as specialization constants (`banjo_bool_spec.h`) | each bool a shader tests (`BOOL_BIT(N)`) is specialization constant 1000 + N, default 2 = read at run time, so the generic pipeline is the old shader; the driver folds the blocks a specialised pipeline does not run | 34,660 constants in 3,627 shaders | `BANJO_RENUT_BOOL_SPEC=0`; key `banjo_gpu_bool_spec` |
| Bone rows in one load | a memory fetch whose offset and stride are whole rows (multiples of four words) loads its four words in one 16-byte load (every fetch copy starts 16-byte aligned) | skinning vertex shaders: 12 aligned 128-bit loads, no word loads | `BANJO_VFETCH_ROWS 0` in shader_common.h |
| Bone fetches without their branch | the predicated-block pass runs a memory fetch for every vertex in its clamped form (`vfetchMemoryClamped`: the address kept inside the draw's fetch data, whose size is SharedConstants word 972), as banjo-nx's bone padding lets its translator do | 2,844 fetches in flattened blocks | `BANJO_RENUT_PRED_FETCH=0` |
| Literals from the constant bank | a literal component NAK cannot encode as an immediate (any of the low 12 mantissa bits set) is read from the shader's own constant register, a free operand; the renderer writes the shader's literal values into those registers of every draw's constants (`g_shaderLiterals`), as the console's Direct3D does at SetShader, so it is exact without banjo-nx's per-draw check | 53,190 components in 3,954 shaders | `BANJO_RENUT_LITERALS_CBUF=0` |
| Blend skip | pixels the pipeline's blend would not change are discarded at the shader's exits (specialization constant 900; a NaN output never) | every pixel shader | key `banjo_gpu_blend_skip` |
| Early alpha | `tools/renut_early_alpha.py` runs banjo-nx's `tools/early_alpha.py` on Renut's HLSL between two runs of the translator (the first dumps, the second compiles the rewritten files, `BANJO_HLSL_OVERRIDE`; a file that does not compile falls back to the shader's own text): the copy of what the alpha needs, self-checked against the original on random inputs, and a 2x2 quad whose pixels all fail the test stops there. Renut tests the draw's alpha function at run time (a uniform branch), where banjo-nx specialises it | 3,663 of 3,936 pixel shaders (197 have too little after their alpha, 76 use forms the tool does not read; none differed) | CMake `BANJO_RENUT_EARLY_ALPHA=OFF` |
| Tessellation grid | `renutVertexRegisters` kinds 3 and 4 (see the GPU table) | | key `banjo_gpu_tess_grid` |

Carried over in package 26: constants through uniform buffers, branchless select, clamp bounds from the constant bank,
predicated blocks without a branch, the branchless fetch swap and helpers.

Not needed with Renut's translator (each checked over the whole library):
- **structured control flow**: Renut's own structurizer leaves no shader in the `switch (pc)` loop form (0 of 4,881);
- **texcoords unswapped**: no texcoord swap in any shader body;
- **texture signs** (`no_texture_signs`, `texture_signs_per_slot`, `sign_mask_skip`): Renut's fetches carry no sign or
  gamma code (the host texture formats do it);
- **identity remap**: the input assembler does the remap;
- **vertex index base held**: 938 of 945 vertex shaders read the vertex index (memory fetches by it): 7 would gain;
- **spec per stage**: Renut's pipelines compile their stages together (`banjo_renut_linked_stages`);
- **EDRAM checks on the CPU**: no EDRAM; **unwritten / pruned vertex outputs**: the linked compile drops them.

### Corrections to sections 4-5

- **Tiles once** needed nothing: Renut already draws a tiled scene once (see section 5).
- **Bone padding**: what it removed in banjo-nx's translator does not exist here; the remaining branch is now gone by the
  clamped fetch above.
- **Collapsed MSAA** stays a config (`banjo_renut_nomsaa.toml`): no MSAA changes without the user's say.

### Measured on the PC (2026-10-17, the console's settings, intro route into Spiral Mountain)

Both renderers uncapped on the same route (`frame_cap = Off`): after the loading (from 50 s), Renut's renderer
102 fps on average, banjo-nx's 56: 1.83 times (median of the 10 s windows 1.72; 1.4 in the heaviest windows, 2.6 in
the lightest; 1.2-1.5 during the first loading). With the 30 fps cap: 30.00 after the loading, every capture right
(logo, title, menus, intro, hub, gameplay; alpha-tested foliage and the tessellated water included), no magenta, no
pipeline failure. The console is the judge: the PC's CPU hides most of what the A57 pays for.

The final build (with the codegen round, `docs/codegen-enhancements.md`): the route with captures (30.00 fps, every
picture right, no magenta), and a verify run with Renut's renderer (`banjo_cpu_verify`, 280 s): every generated-code
self-check at 0 differences (3.55 billion flush-mode checks, 2.51 billion folded constant loads, 106 million promoted
indirect calls, 462 million r1 mirror checks, 176 million link-register write-backs), the localized copies' guard
45.4 million calls compared, 0 differences, and the pictures right at 30 fps. Since this package Renut's renderer's
10 s report also prints those self-checks (the `[verify]` lines and any DIFFERENCE), which only banjo-nx's renderer did
before. The pipeline list (`pipeline_list.bin`, list version 2: the bool words are in the key) holds 2,348 pipelines
from the PC route (947 in package 26's): the console builds them in the background on the first boot.

### Package 27b (2026-10-17): the console boot crash

The first console boot with Renut's renderer (package 27) ended in a memory access fault in
`VmaDeviceMemoryBlock::Map`, from `UploadRing::AddChunk` in `Video::CreateHostDevice`: a mutex read at an address
with the heap's high bits and nothing below. The NRO links two Vulkan Memory Allocators, plume's (3.2.1) and the SDK's
(3.3.0, `sdk/src/ui/vulkan/vulkan_mem_alloc.cpp`). Package 26 renamed plume's C functions (`plume_vma*`), which was
enough on Windows, where the SDK's copy is inside `rexruntime.dll`. On the Switch both are static code, and VMA's C++
internals had the same names in both: 290 weak member functions, vtables and template instances. The linker kept one
of each, so plume's allocations ran through the SDK's 3.3.0 code. Fixed by `tools/plume_vma_names.py`, which renames
every `Vma*` identifier of plume's header (128 of them, plus the 71 C functions); `--check app/out/switch` lists
any VMA symbol defined in both libplume.a and the SDK's libraries (0 after the fix). A scan of every input of the
Switch link for other symbols defined twice found only the same source compiled into two targets, the hooks (a
strong `sub_X` over the generated weak one) and two copies of xxhash's C API (outputs fixed by its specification).
Renut's renderer had therefore never run on a console before package 27b.

### Package 27c (2026-10-17): the console's first runs

Package 27b ran on the console (newlogs25): about 20-30 fps in the first session (overclocked), 9-23 fps in the second
(stock clocks), the render thread the limit in most windows (40-107 % of core 1), the GPU in the heaviest (the main
1280x720 pass three quarters of it). The render thread's largest single cost was the stream checks' hashing (23 % of
its samples). Three faults and that cost, fixed here:

- **The Status Monitor's FPS** stayed at an old value (19.4-19.7) and the profile said "game 0.0 fps": the console's
  presented-frame counter (`RexSwitchPerfCount(0)`, which SaltyNX's FPS row and the profiler read) was counted only by
  banjo-nx's renderer and the SDK's. Renut's present counts it now (`perf/frame_stats.cpp`). The profiler window also
  gets its "GPU Frame" and "Present" rows from Renut's renderer, and a new "Render thread (Renut)" row.
- **One-frame flashes** (white, teal: the scene behind the title's logo replaced for one frame, the HUD drawn right;
  two videos from the user): every kick queued the conversion of its interval's vertex data before its fence
  completion, but the Swap queued only the fence, and the frame's last vertex data was converted inside the present,
  after the game had been told the frame was done; callbacks converted pending textures first but not vertex data;
  kicks converted vertex data but left texture uploads to the submit. A game that rewrites a dynamic buffer or texture
  as soon as it is free could have its new bytes taken for the old frame's last draws (the composite, the HUD), the
  more likely the longer the render thread is held at that point (a compile, the swapchain's acquire). Every
  completion point (kick, Swap, callback) now converts both first (`banjo_renut_checks::QueueKickFlush`). This is the
  candidate fix, made from the code: the PC did not show the flash even with compiles slowed to the console's speed
  (`banjo_renut_diag_build_delay_ms`, 600 consecutive frames of the title checked), so only the console can confirm it.
- **The pipeline workers' priority** (0x3F) was refused by the console ("NOT accepted"); 0x3B now, as
  UnleashedRecomp-NX's compiler thread.
- **Stream checks:** measured on the PC first: 7 kicks a frame; 20-30 % of the checks (10-17 % of the bytes) are of a
  copy already checked in the frame; the game asks about fences in few windows (0 in most of the level's), but about
  20 guest functions read the fence completion word directly, so a once-a-frame rule could not be proven and is not
  used. Done instead, all exact:
  - `banjo_renut_check_worker` (A/B 10): a worker thread hashes, at each kick the game issues, the ranges that interval
    checked in the last frame (every draw of the interval is issued by then, and nothing they read may change before
    the render thread has processed the kick); the render thread takes those hashes. PC: 77-90 % of the checks
    answered by the worker; verify run, every result hashed again: 0 differences in about 410,000 per 10 s.
  - A memo of the ranges hashed in the interval (a buffer cached under several stream or index keys is read once):
    0-5 % of the bytes.
  - A prefetch in the PMULL hash's loop (the data is cold: the loop ran at about 1.5 bytes a cycle on the console
    against the 8 its multiplies allow).
- **Counters** in the `[renut]` line: repeat checks in a frame, interval-memo answers, kicks, fence queries; a
  `[renut] check worker` line (and `[verify] check worker` in verify mode).

### Package 28 (2026-10-17): UnleashedRecomp-NX / MarathonRecomp-NX's GPU items, and Renut's renderer by default

What their docs list was checked against Renut's renderer and the console's figures (newlogs25 with the timer
corrected: at stock clocks the GPU took 35-70 ms a frame in the busy windows, depth resolves about 3 ms and colour
resolves about 3.6 ms of it; the render thread up to 55 ms). Done, all exact:

- **GPU timestamps** (plume, the Switch): NVK on Horizon reports a timestamp period of 1 ns; the Tegra X1's timer ticks
  at 614.4 MHz (1.6276 ns, the factor banjo-nx's renderer and UnleashedRecomp-NX use). Every GPU figure of Renut's
  renderer on the console read 61 % of the real one; corrected in `plume_vulkan.cpp`.
- **Fewer resolve copies** (`banjo_gpu_depth_words_view`, A/B 13): the game reads nearly every depth resolve back as
  an 8_8_8_8 texture over the same memory (the depth as guest words). Renut copied the whole words texture into that
  texture at its first bind after each resolve; it now samples a view of the resolve's words texture (same format,
  size, one level; `WordsViewSlot` in `render/textures.cpp`). Every draw binds its textures again, so the draws after
  a resolve read its words either way. PC route: every such bind through the view (about 600 per 10 s, 0 copies);
  in-run A/B within noise on the PC (median 0.09 ms a frame over 22 windows, 12 of them better): a GPU with bandwidth
  to spare. The console's A/B decides.
  Tried first and dropped: the words written only when read (a depth resolve writing its R32F destination alone):
  every depth resolve's words were read (1,500 resolves, about 1,500 rectangles made later per 10 s), so it only
  added a pass.
- **Trimmed pixel outputs** (`banjo_gpu_output_trim`, A/B 12; `docs/shader.md`): the colour channels a pipeline never
  writes are 0 from the pixel shader's exits on (specialization constant 901 from the key), so NAK drops the work that
  made only them. The key holds the colour mask already, so there are no new pipelines.
  `banjo_renut_diag_trim_poison` is its test: the trimmed channels get 60000, and the picture must not change (PC
  route: no visible change; in the standing scene the count of saturated pixels within 1 % of the normal run's, where a
  leaked channel would whiten whole surfaces). PC A/B: no change (median 0.00 ms; the PC's driver knows the write mask
  when it compiles, NAK does not). Its A/B's off half compiles the untrimmed pipelines on first use; the package's
  pipeline list drops those twins (1,680 of them from the PC's A/B run).
- **Depth-only draws without their pixel shader** (`banjo_gpu_depth_only`, A/B 11): no colour written, a depth
  target, no alpha test, no alpha to coverage, and a shader that does not kill, write depth or export. In the routes
  played, all of the about 42,000 such draws per 10 s use one of 27 shadow-caster shaders whose kill is the game's
  alpha cut-out (one fetch, a compare, the kill), so 0 qualified. It stays on for any that does.
- **Pipeline cache saved after a quiet period** (`banjo_renut_cache_save_quiet`; Unleashed saves at loading
  screens): once no pipeline was built for 15 s (at most once a minute, after 5 minutes of compiling regardless),
  instead of every minute with new pipelines. The save holds the driver's cache lock, which a compile on the render
  thread waits for.
- **Diagnostics**: `banjo_renut_diag_resolve_trace` (one frame every 30 s of resolves, clears, target changes and the
  first sample of each resolved texture, as `[trace]` lines) and a census in the `[renut]` line (depth-only draws,
  draws writing nothing, part of a target's channels).

Checked and not taken:

- **Compact texture heap**: Renut's heap has 8,192 descriptors (32 KB at NVK's 4 bytes), under the 64 KB NVK reads
  from a constant bank. Unleashed shrank theirs from 65,536 to get there.
- **Hand-over or lazy resolves**: the trace of a gameplay frame shows the main scene's 2x MSAA colour and depth
  resolves, the shadow maps' depth resolves (single-sampled) and the post-processing chain (1280x720 down to 80x45,
  single-sampled, about 2.4 Mpx a frame). A hand-over would need the next draw to cover the whole surface, which the
  CPU cannot prove (the positions come from the vertex shader). A lazy resolve would save nothing in a ping-pong: the
  surface is drawn again before its texture is resolved again. Resolve-and-clear happens only on the MSAA targets, so
  hand-over at clears does not apply. Carried clears and dead copies caused GPU loss in banjo-nx's round 10 and are
  not repeated.
- **Render-pass restarts**: about 15 a frame on the main 1280x720 target, each after one draw that reads memexport
  results through vertex fetch, whose copy into the fetch buffer (and its barriers) ends the pass. Avoiding them
  needs those reads to address the export buffer directly: left for a later round.
- **Redundant binds, sampler and pipeline lookups, constants of draws without a pixel shader**: Renut already filters
  them (index buffer, pipeline, layout, constant sets, the sampler memo, the last-key pipeline lookup; a draw without
  a pixel shader uploads no pixel constants).

`renut.toml` (the Switch) boots Renut's renderer since this package (`banjo_renderer = "renut"`, every
`banjo_renut_*` / `banjo_gpu_*` key it reads at its default of on, both master switches on), with `renut_gpu_profile =
10` and the resolve trace. `configs/banjo_renderer_native.toml` and the configs of banjo-nx's renderer's own keys
(`NATIVE_ONLY` in `tools/make_enhancement_configs.py`) boot banjo-nx's renderer.
