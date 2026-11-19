# Renut's renderer in banjo-nx: the port

The Switch build's renderer is reNut upstream's Direct3D-level renderer (`github.com/masterspike52/reNut`): N&B's Direct3D
calls run natively through plume. No PM4, no command ring, no EDRAM model. On the Switch plume's back end is deko3d
(`docs/deko3d-renderer.md`); on the PC it is Vulkan or D3D12.

Credit: Renut by masterspike52 and contributors (the renderer has no licence file; it is used with the author's
permission). Its XenosRecomp fork (`thirdparty/reblue-XenosRecomp`) is MIT, and plume is MIT.

This page is how the renderer was brought into banjo-nx and fitted to the A57, with the keys, the files and what was checked.

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

`renut_app.h`: `OnPreSetup` still applies the Switch settings (`banjo_gpu_boost`, ...), then sets no SDK graphics system and
no GPU plugin (`renut::gpu::ConfigureRuntime`); the renderer starts from `OnFinalizePaths` and gets the SDK's ImGui drawer for
the menus. The CPU side (codegen rounds, hot functions, hand-written natives, critical sections, the log writer, reverb) is
the game code's.

### Shaders

Renut's renderer takes its shaders from Renut's XenosRecomp, keyed by the XXH3 of the guest container:

- Input: Renut's own dump of `default.xex` and the bundles (`reNut/tools/dump_shaders.py` → `out/renut_shader_dump`, 4,731
  containers), plus the shaders a run captured because the cache lacked them (`app/out/pc-me/shader_capture`, 150 on the intro
  route: shaders the game makes itself). Game data: never committed or shipped apart from the NRO.
- XenosRecomp is a host tool. The PC build generates `app/out/pc-me/renut_gpu/shader_cache.cpp` and
  `python tools/renut_switch_shader_cache.py` makes the Switch's shader table from it. **Build the PC tree first after any
  translator change**, then run the script. From there to the console: `docs/shader.md` and `docs/deko3d-renderer.md`,
  sections 5 and 30.

## 2. Test tools

| Change | Why |
|---|---|
| Frame rate in the log: `[frames] N game frames in 10.0 s: X fps (Renut's renderer)` | one figure to compare runs by (D3DDevice_Swap) |
| Test captures (`banjo_renut_capture.h`): `nfsmw_captura_cada_s`, `banjo_video_s` and `banjo_record_s` work with Renut's renderer | they read the SDK presenter, which Renut's renderer does not use; the next Present scans the frame out once more into an offscreen target and reads it back (a GPU wait per capture: a test tool) |

## 3. The Switch

| Part | What |
|---|---|
| driver | None: the back end writes deko3d commands, and the Switch build links no Vulkan driver (`docs/deko3d-renderer.md`, section 23). |
| window | `backend/native_window.cpp`: libnx's default `NWindow`. |
| threads | `std::thread::detach` throws on the Switch (package 24's crash): Renut's prebuild workers detached themselves. They and the render thread are now threads that are never joined, made with pthreads and explicit stacks on the Switch (render 1 MB, workers 512 KB; `StartLongLivedThread`), std::threads elsewhere. |
| Dear ImGui | one copy on the Switch (the SDK's, same 1.92.5): Renut compiles its own elsewhere to keep its overlay's context away from the SDK's, which a static link would define twice. Renut's own overlay is not started on the Switch; the SDK's menus are drawn. |
| files | `pipeline_list.bin` (Renut's list of the pipelines to build ahead) and `shader_capture/` (shaders the cache lacked) are next to the executable (the NRO's folder), not in the working directory. |

## 4. Towards the A57

Every item with a key is in the enhancement table (`src/cpu/banjo_enhancements.cpp`, the `[enhancements]` log line) and goes
off with its group's master switch.

| Key | Group | Default | What | Same output? |
|---|---|---|---|---|
| (build) constants in uniform buffers | translator + renderer | on | The guest shaders read their float constants and SharedConstants from uniform buffers instead of raw loads through 64-bit addresses: Maxwell's constant banks, where a constant is a free operand. On deko3d they are pushed by what changed (`banjo_deko3d_push_constants`). | Yes: the same bytes at the same offsets. |
| (build) branchless select | translator | on | The scalar predicate updates and the kill tests are `select()` instead of HLSL 2021's short-circuit `?:`, which DXC writes as branches. | Yes: no side effects in the operands. |
| (build) predicated blocks without a branch | translator | on | Renut's translator wraps nearly every predicated instruction and exec block in `if (p0)` (190-390 a heavy pixel shader). A pass over its output (`banjo_pred_select.h`) turns a block of plain register arithmetic (targets rN, ps, p0; no fetch, load, kill, output, address register, derivative or indexing; at most 12 statements; not one with an `else`) into `lhs = select(pb, (type)(rhs), lhs)` with the predicate taken at the block's entry, nested blocks under `and(outer, p0)`. 244,644 blocks over the 4,881 shaders; the four largest pixel shaders: conditional branches 231 -> 80, 415 -> 93, SPIR-V instructions -17 to -20 %; compressed SPIR-V 3.87 -> 3.59 MB. Build-time: `BANJO_RENUT_PRED_SELECT=0` in the build's environment turns it off. | Yes: each statement runs in order when the predicate holds and each target keeps its value when it does not; the targets are temporaries, the right sides have no side effects. Pictures of the route unchanged. |
| (build) branchless vertex fetch swap | translator | on | `vfetchSwap` (the endian swap of every word a vertex fetch from memory reads: bone palettes, instance data) chose its swap with an if-chain on the draw's endian mode, a branch per word; now the three swaps and `select()`. Skinning vertex shaders (12 bone fetches): conditional branches 178 -> 25 (with the predicated blocks), SPIR-V -32 %; compressed SPIR-V 3.59 -> 3.43 MB. | Yes: the same word for every mode (checked over 800,000 random words). |
| (build) branchless helpers | translator | on | The alpha test picks its comparison with `select()` instead of a `switch` on the draw's function (an `OpSwitch` per test); `rcpf`/`rsqf` and the texture scale decode use `select()` instead of `?:`. Compressed SPIR-V 3.43 -> 3.30 MB. | Yes: the same comparisons and values, NaN included. |
| (build) clamp bounds from the constant bank | translator + renderer | on (SPIR-V) | The Xenos RCP / RSQ / LOG clamps take FLT_MAX from SharedConstants' word at 1340 (FLT_MIN is its negation) instead of 32-bit immediates moved into a register before every clamp. | Yes: the same value. |
| `banjo_ring_core` | (parameter) | 1 | Renut's render thread pinned to core 1, priority 0x2C. Renut started it as a plain `std::thread`, which libnx puts on the process's default core with the game. | Yes: only where it runs. |
| `banjo_background_compile_core`, `banjo_prewarm_threads` | (parameters) | 2, 1 | Renut's pipeline prebuild workers and its content verifiers (the threads that re-hash cached vertex and index copies): on core 2, priority 0x3F; one prebuild worker (Renut starts half the cores'). | Yes. |
| `banjo_renut_upload_cached` | CPU | `true` (Switch) | The upload ring (constants, converted vertices and indices, texture uploads) in CPU-cached memory, published with a flush of what was written before each submission (plume: `RenderBufferFlag::HOST_CACHED`, `unmap(0, &writtenRange)`), instead of write-combined memory: 5,251 MB/s against 2,506 on the A57. PC: write-combined (cached memory is slower for its discrete GPU to read). | Yes: the same bytes reach the GPU. |
| `banjo_renut_push_point_only` | CPU | `true` | With the constants in uniform buffers only the point sprites' geometry shader reads the draw's push constant (the three constant addresses): only point-list draws push it. | Yes: no other shader reads it. |
| `banjo_renut_capture_pool` | CPU | `true` | A queued draw's capture (its 12.7 KB device snapshot, the copied vertices and indices, the closure the render thread runs) comes from a pool and goes back to it once drawn, instead of three heap allocations per draw on the game thread freed on the render thread (about 2,000 a frame through the Switch's single malloc lock); the closure holds a plain pointer, which `std::function` stores without allocating. PC, same route: the frame rate up in most 10 s windows. | Yes: the same data. |
| `banjo_renut_shared_dedupe` | CPU | `true` | A draw whose SharedConstants (1,344 bytes: texture and sampler slots, booleans, render state) equal the previous draw's on the same command list reuses that upload: no copy into upload memory and no set-7 bind. PC, intro route: 20-28 % of about 1,500 draws a frame; the `[renut]` line counts them. **Beneficial** on the console (A/B target 3, 2026-10-30, da8ea3599258: render thread -0.30 ms a frame in all 4 city pairs, +0.12 fps). | Yes: exact compare of the bytes. |
| `banjo_renut_queue_notify_idle` | CPU | `true` | Work queued for the render thread (every draw) wakes it only while it waits in its condition variable, instead of a notify per item: on the Switch every notify is a kernel call, about 1,500 a frame from the game threads, most while the render thread is busy. The render thread reads the queue under the same lock before it sleeps, so no push is missed (the same rule as the SDK's `thread_signal_skip_idle`). | Yes: timing only. |
| `banjo_renut_neon_swap` | CPU | `true` (ARM) | The vertex conversion's byte swaps (guest big-endian words to the host's) 16 bytes per NEON `rev16`/`rev32` instead of a word at a time; four-word elements are one vector. Checked against Renut's `SwapDword` through simde: `tools/tests/renut_neon_swap_test.cpp`, 600,000 random vectors, 0 mismatches. | Yes: the same bytes. |
| `banjo_renut_pmull_hash` | CPU | `true` (ARM) | Renut's per-frame content hashes of guest data (vertex streams, index ranges, texture samples, CPU-written textures) use banjo-nx's PMULL-folded CRC-64 (`banjo_pmull_fingerprint.h`) instead of XXH3: half the instructions per byte on the A57. A self-test against the bit-by-bit reference at start (any difference keeps XXH3). The shader containers keep XXH3 (the cache is keyed by it). | Yes: the hashes only compare with themselves; 2^-64 odds. |

## 5. Tiled drawing

Renut draws a tiled scene once. A surface bound between `BeginTiling` and `EndTiling` is backed by a target covering every
tile, the scene's draws run once into it, and D3D's replay of the tiles' ring segment per tile (its WorkerDpc callback) is
left out (`d3d/hooks/draw.cpp`). What stays per tile is what the game itself issues per tile (draws predicated to one tile,
cut to its rect: the vehicle parts' per-tile command buffers, the water with each tile's constants) and `EndTiling`'s resolve
of each tile's rect: the console's work, not emulation.

## 6. Checking it

- In-run A/B: `banjo_renut_ab_target`, half-period `banjo_renut_ab_s`, the `[renut A/B]` line every 10 s
  (`configs/banjo_renut_*_ab.toml`; `docs/enhancements-reference.md`, "How to measure"). Keys that change memory types or
  hashes at start need whole-run configs.
- Renut's configs set `renut_post_process_precision = "Game"`: the bloom, depth-of-field and SSAO buffers in the console's
  8-bit format (Renut's default "High" makes them 16-bit: twice their bandwidth on the Switch, and an image that differs from
  the console's); `configs/banjo_renut_post_high.toml` is Renut's default.
- In the log, with logging on: `[enhancements]`, `[renut]` (thread placement, content hashes), `[frames]` every 10 s, `GPU
  profile` with `renut_gpu_profile`.

## 7. Package 27 (2026-10-17): exactness and memory

Each item has its key. Where it can be switched while running, it has an in-run A/B target of `banjo_renut_ab_target`.

### Exact and safe on the console (not enhancements: no master switch)

| Key | Default | What | Checked |
|---|---|---|---|
| `banjo_renut_exact_streams` | `true` | Renut keeps converted copies of vertex streams and index ranges whose guest bytes held for a few flushes, and checked them on a background thread once every 4 frames: a range the game rewrote could draw from its old copy for about 5 frames. Now a copy is hashed whole on its first use after each kick and converted again if its bytes changed: Renut completes a kick's fence as soon as the render thread has read the kick's data, so after any kick the game may know the GPU is done with a buffer and write it again (also after memexport results land in guest memory). What it does not cover, like Renut's own check: a write the game makes after the draw call and before the kick (Direct3D allows it; Renut reads uncached streams at the kick, but a cached copy is chosen and hashed when the render thread takes the draw, which can be earlier). In-run A/B target 6. | PC route: 450,000-505,000 checks of 5.4-7.1 GB every 10 s (18-24 MB a frame), 0 stale copies caught in any run, and Renut's own check (key off, `configs/banjo_renut_exact_off.toml`: the `[renut]` line counts the copies it finds late) found none either. Cost, PC in-run A/B (17 windows after the loading): render thread 5.00 -> 6.47 ms a frame (+1.47 ms, dearer in every window). On the A57 the same bytes at about 3 GB/s (the PMULL hash) would be 6-8 ms a frame: read the console's A/B before judging Renut's renderer's speed. A cheaper rule that stays exact (hash only when the game can have seen a fence since the copy's last use) needs the fence count carried with each queued draw, recorded command buffers included: not done. |
| `banjo_renut_memory_policy` | `true` on the Switch | Renut's limits were sized for a PC's video memory. Cold host textures (not bound for 120 frames) are released when less than `banjo_renut_textures_reserve_mb` (384 on the Switch) of the budget is left or the bindless table is three quarters full, checked every 32 frames; the stream cache stops at `banjo_renut_stream_cache_mb` (128; Renut 768) and drops copies idle `banjo_renut_stream_cache_idle_s` (20 s; Renut 120); upload ring chunks a heavy frame added go after `banjo_renut_upload_trim_frames` (120; Renut 600). A released texture is made again from guest memory when bound. Rules from sa2-nx's console experience (its glitches came from too little eviction and stale resolve memory, not from evicting): only cold textures, never one the GPU wrote. | Forced-eviction run (`configs/banjo_renut_eviction_test.toml`: a reserve larger than any budget, released slots poisoned magenta): no magenta in any capture, the same pictures. |
| `banjo_renut_keep_gpu_written` | `true` | A host texture the GPU wrote (a resolve destination; Renut writes nothing back to guest memory) is never released by an eviction rule: made again from guest memory it would hold whatever that memory holds. Renut's own backstop released them after 3,600 idle frames. | |
| `banjo_renut_diag_poison_slots` | `false` | A test: descriptor slots given back point at a magenta texture instead of the null one (sa2-nx's poison test). | |
| memory report | always | On the Switch a `Memory: heap` line: the malloc heap in use, its size (1,024 MB, half of it the guest's memory) and its free pieces. | |

### GPU keys

| Key | What | Measured / checked |
|---|---|---|
| `banjo_gpu_bool_spec` | The renderer's side of the bool constants (`docs/shader.md`): the pipeline key's bool words, and the generic pipeline while a prebuild worker makes a specialised one. In-run A/B target 7. | PC route: about 110,000 of 226,000 draws a 10 s window draw with specialised bools; new combinations draw with the generic pipeline for a few frames |
| `banjo_gpu_blend_skip` | The mode comes from the pipeline key (one UNORM target, no depth or stencil write, a blend that keeps the destination for a zero source). In-run A/B target 8. **Off**, with `banjo_renut_blend_skip`: `docs/deko3d-renderer.md`, section 11. | |
| `banjo_gpu_tess_grid` | Tessellated draws indexed by grid vertex: the vertex shader runs once per grid vertex instead of once per triangle corner. The host's index list (one per kind and level, made once) names the same triangles in the same order with the same corners, and each grid vertex gets the same weights. In-run A/B target 9. | PC route: 3.0 million grid vertices for 15.2 million corners every 10 s (the water) |

The translator's changes are in `docs/shader.md`.

### Package 27c (2026-10-17): the console's first runs

- **The Status Monitor's FPS:** the console's presented-frame counter (`RexSwitchPerfCount(0)`, which SaltyNX's FPS row and the
  profiler read) is counted by Renut's present (`perf/frame_stats.cpp`). The profiler window also gets its "GPU Frame",
  "Present" and "Render thread (Renut)" rows from Renut's renderer.
- **Conversions before each completion point:** every kick queued the conversion of its interval's vertex data before its
  fence completion, but the Swap queued only the fence, so the frame's last vertex data was converted after the game had
  been told the frame was done; callbacks converted pending textures first but not vertex data; kicks converted vertex data
  but left texture uploads to the submit. A game that rewrites a dynamic buffer or texture as soon as it is free could have
  its new bytes taken for the old frame's last draws. Every completion point (kick, Swap, callback) now converts both first
  (`banjo_renut_checks::QueueKickFlush`).
- **The pipeline workers' priority** (0x3F) was refused by the console; 0x3B now, as UnleashedRecomp-NX's compiler thread.
- **Stream checks:** 20-30 % of the checks (10-17 % of the bytes) are of a copy already checked in the frame, but about 20
  guest functions read the fence completion word directly, so a once-a-frame rule could not be proven and is not used. A memo
  of the ranges hashed in the interval (a buffer cached under several stream or index keys is read once) gives 0-5 % of the
  bytes, and a prefetch in the PMULL hash's loop helps the cold data.
- **Counters** in the `[renut]` line: repeat checks in a frame, interval-memo answers, kicks, fence queries.

### Package 28 (2026-10-17): UnleashedRecomp-NX / MarathonRecomp-NX's GPU items

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
  writes are 0 from the pixel shader's exits on (specialization constant 901 from the key), so the compiler drops the work that
  made only them. The key holds the colour mask already, so there are no new pipelines.
  `banjo_renut_diag_trim_poison` is its test: the trimmed channels get 60000, and the picture must not change (PC
  route: no visible change; in the standing scene the count of saturated pixels within 1 % of the normal run's, where a
  leaked channel would whiten whole surfaces). PC A/B: no change (median 0.00 ms; the PC's driver knows the write mask
  when it compiles). Its A/B's off half compiles the untrimmed pipelines on first use; the package's
  pipeline list drops those twins (1,680 of them from the PC's A/B run).
- **Depth-only draws without their pixel shader** (`banjo_gpu_depth_only`, A/B 11): no colour written, a depth
  target, no alpha test, no alpha to coverage, and a shader that does not kill, write depth or export. In the routes
  played, all of the about 42,000 such draws per 10 s use one of 27 shadow-caster shaders whose kill is the game's
  alpha cut-out (one fetch, a compare, the kill), so 0 qualified. It stays on for any that does.
- **Diagnostics**: `banjo_renut_diag_resolve_trace` (one frame every 30 s of resolves, clears, target changes and the
  first sample of each resolved texture, as `[trace]` lines) and a census in the `[renut]` line (depth-only draws,
  draws writing nothing, part of a target's channels).
