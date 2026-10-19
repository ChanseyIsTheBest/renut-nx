# Removing the remaining EDRAM emulation: analysis and plan (2026-10-15)

Read-only analysis. Nothing in `D:\Banjo kazooie port` was changed, built or run. The only tool run was the
symbolizer `tools/switch_hot_symbols.py`, on the console profile of package 19 and the archived ELF
`builds/2026-10-15_1140_diagnostics/renut.elf`. The helper scripts and log cut-outs are in this scratchpad:
`gpuswap.py`, `win_avg.py`, `prof19_level.log`, `prof19_all.log`.

Sources:
- Console log `dist/switch/banjo-nx/newlogs19diagnostics/renut_001.log` (package 19), plus `rex/rex_profile.log` and
  `rex/rex_stderr.log`. The run was at stock clocks for its whole length: every one of the 63 profile reports says
  `clocks: CPU 1020.0 MHz, GPU 460.8 MHz, memory 1600.0 MHz`.
- PC logs `v20A.log` (before `banjo_gpu_edram4_clear_merge`) and `v20B.log` (after it). Both use the console toml
  and the intro route.
- Code: `app/src/me/masseffect/masseffect_native_targets.cpp` (abbreviated **T** below),
  `masseffect_native_draws.cpp` (**D**), `app/src/me/me_native_system.cpp` (**S**), `app/src/me/banjo_d3d_clear.h`.
- Docs: `docs/enhancements-reference.md` ("EDRAM", "Command recording"), `docs/enhancements.md`,
  `docs/d3d-native.md` §9. Memory notes: renderer findings, Direct3D mandate, console runs.

---

## Status (2026-10-16)

| Step | State | Key |
|---|---|---|
| 1a stencil fetches in the 3D engine | **done** (the whole range; the per-draw tile limit was tried and dropped) | `banjo_gpu_stencil_fetch_3d`, A/B 32 |
| 1b 1x depth views with ZCULL | written, **off** (console-only check; UnleashedRecomp's GPU loss with ZCULL planes and depth-clear changes) | `banjo_gpu_depth1x_zcull` |
| 2 barriers around EDRAM clears | **done** (22 call sites) | `banjo_gpu_edram4_lean_barriers`, A/B 33 |
| 3 depth resolve | barriers narrowed (part of step 2's key) and its time counted as `copies`; the merged dispatch not done | `banjo_gpu_edram4_lean_barriers` |
| 4 skip the second tile pass | not done (effects to replay) | - |
| 5 resolve clears in one pass | **done** (colour; depth only without a ZCULL plane) | `banjo_gpu_resolve_clear_merge`, A/B 36 |
| 6 loading transitions | not done (needs a trace) | - |
| 7 per-view memo epochs | not done (small gain, ownership risk) | - |

Details and measurements: `docs/enhancements-reference.md`, "EDRAM".

## 0. Short version

Steady-state tile transfers are already **0** on both machines (`sync cache: 0 tiles skipped, 0 transferred`). The
EDRAM emulation that is left costs time in four ways. None of them is a tile transfer.

1. **The lazy-stencil "late fetch".** Once a frame, 96 tiles of stencil go through the copy engine. That is
   `edram_import9` = **1.3 ms a frame at stock**, steady in both the intro and the level. Because the same copy-engine
   path exists, every 1x depth view is created with `TRANSFER_DST`, so **neither shadow map gets a ZCULL plane**. The
   shadow maps are the only depth targets whose depth direction matches the driver's ZCULL direction ("less").
2. **Full barriers around EDRAM operations.** Package 19 had **420 full flushes a frame** on the console. Package 20's
   clear merge brings that to about 47-62 on the PC. On the PC alone, the merge took **2.7 ms off the busy GPU time**
   (matched intro windows: 14.8 -> 12.1 ms; the "640" category, which holds the shadow maps, went 6.46 -> 4.54 ms).
   About 50 full flushes a frame remain, all redundant with render-pass dependencies or wider than needed.
3. **The scene-depth resolve conversion.** "guestspace depth resolve" runs 6 times a frame (3 resolve points x 2 tile
   halves). It costs `edram_alias` = **1.9 ms a frame at stock** and 12 full barriers. It is a real resolve, but each
   tiles-once pair could be one dispatch.
4. **The second predicated-tile pass.** It is still parsed: **2.3-3.5 ms a Swap of ring time at stock**. The intro is
   ring-bound at stock (fence waits only 39-505 ms per 10 s), so this matters there.

On top of that, tile bookkeeping is about 3.5 % of the ring thread's samples. It is not worth an architecture
rewrite now.

Ranked steps (section 3):

| # | Step | Expected console saving (stock) | Risk |
|---|---|---|---|
| 1a | Stencil-only late fetches in the 3D engine, limited to the draw's tiles (`banjo_gpu_stencil_fetch_3d`) | about 1.1 ms GPU a frame (import9 1.3 -> about 0.1-0.2) | low |
| 1b | 1x depth views without TRANSFER_DST, so they get ZCULL planes (`banjo_gpu_depth1x_zcull`) | unknown, up to about 1-2 ms of the "640" category (shadow maps 2.6-6.8 ms) | medium (driver ZCULL) |
| 2 | Remove or narrow the full barriers around EDRAM clears, resolves and conversions (`banjo_gpu_edram4_lean_barriers`) | about 0.5-1.5 ms GPU (around 40 of about 50 full flushes a frame) | low-medium |
| 3 | One depth-resolve dispatch per tiles-once pair, narrowed barriers (`banjo_gpu_depth_resolve_merge`) | about 0.3-0.7 ms of the 1.9 ms | low |
| 4 | Skip the second tile pass's command buffers when its entry state is proven equal (`banjo_ring_tiles_once_skip`) | 2.3-3.5 ms ring a Swap (intro: ring-bound) | medium-high |
| 5 | Resolve clears and the tiles-once clear: one pass per image instead of one per rectangle (`banjo_gpu_resolve_clear_merge`) | about 0.2-0.5 ms (about 10 passes and 20 full flushes a frame) | low |
| 6 | Loading-transition depth representation switches (trace first) | 7-8 ms a Swap during the jigsaw transition only | medium |
| 7 | Surface-level Clear and per-view memo epochs (CPU) | up to about 3 % of the ring | medium |

Steps 1a, 1b and the barrier half of step 2 can first be probed on the console **without code**, through existing
keys (section 3, "zero-code probes").

---

## 1. What the EDRAM emulation still does each frame

### 1.1 Overall picture (`GPU per Swap`)

Console, package 19, stock clocks. Level (13:22:27 report):

> `GPU per Swap (90 Swaps): total 110.9 ms; ... scene 79.1 ms ... 640 6.0 ms ... 320 0.8 ms ... smaller 12.2 ms ... copies 4.7 ms ... clears 0.2 ms ... scene_no_z 1.4 ms ... gap 3.4 ms ... edram_import 0.0 ms ... edram_export 0.0 ms ... edram_alias 1.9 ms ... edram_import9 1.3 ms`

Intro (13:20:40 report):

> `GPU per Swap (168 Swaps): total 59.6 ms; ... scene 41.0 ... 640 2.6 ... copies 4.8 ... clears 0.2 ... gap 5.2 ... edram_import 0.0 ... edram_alias 1.9 ... edram_import9 1.3`

`edram_alias` = 1.9 ms and `edram_import9` = 1.2-1.3 ms are flat from 13:20:30 to 13:23:47 in every report, so they
are per-frame constants. The level is GPU-bound (`slot fence waits 86 (2838.9 ms)` per 10 s). The intro is close to
ring-bound (fence waits 39-505 ms per 10 s; ring 54-77 ms of work a Swap against GPU 59-85 ms).

PC (RTX 3060, console toml, intro route), averaged over the matched windows 95-230 s after start
(`win_avg.py`):

| | v20A (no clear merge) | v20B (clear merge) |
|---|---|---|
| busy GPU (total - gap) | 14.77 ms | 12.11 ms |
| 640 (shadow maps) | 6.46 | 4.54 |
| copies | 1.97 | 1.35 |
| edram_alias | 0.34 | 0.42 |
| edram_import9 | 0.09 | 0.09 |
| barriers per Swap | 427-443 (408-436 full) | 69-73 (62 full), later 52 (47 full) |
| render passes per Swap | 239-264 | 79-81, later 65 |
| vkCmdClearAttachments per Swap | 112-125 | 5-6 |

The EDRAM categories are 0.4-0.5 ms on the PC against 3.2 ms on the console. The console pays roughly 5-13x more for
the same operations: engine switches and waits for idle are much more expensive there.

### 1.2 Mechanism by mechanism

**M1. Tile transfers (4x<->1x, depth<->colour, depth representation switches, 9-pass imports).**
- Steady state: **0**. Console level: `EDRAM mode4 operations since last report: depth import=85 guestspace depth
  resolve=516; sync cache: 0 tiles skipped, 0 transferred`. PC v20B: `guestspace depth resolve=1800; sync cache: 0
  tiles skipped, 0 transferred`.
- The loading/jigsaw transition still bursts. Console at 13:20:05:
  `draw/depth-repr-switch=960ops/11520tiles draw/depth->color=480ops/480tiles`,
  `9-pass imports: [D000:1280x1280:mx0my0->000:560x720:mx0my0 480ops/480t]` and
  `copy-engine stencil imports 93 (47616 tiles)`. GPU at 13:20:10: `edram_import 0.9 ... edram_import9 7.0 ms`.
  The same windows hold "single-sample approximation" depth->colour exports (see section 4).

**M2. Lazy-stencil late fetch (the biggest steady EDRAM GPU item).**
- Console level, per 10 s: `lazy stencil: ... 87376 tiles inherited by depth-only draws, ... 85 late fetches (8160
  tiles)` and `copy-engine stencil imports 85 (8160 tiles)`. That is 1 a frame, 96 tiles.
  Intro (13:20:45): `copy-engine stencil imports 156 (14976 tiles)`.
- GPU: `edram_import9 1.3 ms` a frame (console); `edram_import 0.0`. So the empty import render pass is free and
  the copy-engine part costs it all. PC: 0.1 ms.
- PC v20B: `late fetches (28896 tiles)` / `depth import=301` per 10 s from 96 s to 225 s after start (the intro's
  first shots), then 0. That is the window to test in.
- Trigger: a depth-only redirected clear or a depth-only draw hands the scene depth's tiles to another depth view and
  records where the stencil stayed (`StencilSourcesWrite`, T:3835; the reassign at T:5904-5916). Later, a
  **stencil-using draw on that view** runs `FetchStencilDeferredEDRAM4(*image, 0, plan.length_tiles[0])` (T:6497).
  That fetches every deferred tile from 0 to the plan's length, not only the tiles the draw touches.
  `ImportColorDepthEDRAM4` (T:7430) then takes the copy-engine branch, because 96 >= `stencil_copy_min_tiles` (32,
  T:335) and the target is a 1x TRANSFER_DST view (T:7646-7659). It records:
  - an import render pass with **0** draws (T:7544-7570, `steps = 0` at T:7661);
  - a compute dispatch (stencil -> buffer);
  - a barrier;
  - `vkCmdCopyBufferToImage(STENCIL)` on the copy engine;
  - a `MEMORY_READ|WRITE` / `ALL_COMMANDS` barrier (`CopyStencilEDRAM4`, T:7350-7426).

  That is three engine switches for 122,880 pixels.
- An older PC trace (scratchpad `trace4.txt`) names the pair: `EDRAM depth import D000/f1:1280x1280:mx0my1g0h ->
  D000/f1:1280x1280:mx0my0g0h: 42 tiles, 0 passes + stencil by the copy engine`. That is the 2x scene depth read
  through its 1x view, the one real EDRAM reinterpretation Banjo does (`docs/d3d-native.md` §9, step 3). The current
  96-tile instance still needs naming (step 1a, diagnostic).

**M3. Redirected depth clears (Direct3D's Clear drawn through a 4x alias at half pitch).**
- Console level, per 10 s: `redirected depth clears 515 (170274 tiles), declined 0`, which is 6 a frame and about
  1,980 tiles a frame. `depth clears given their 1x view by shape (banjo_gpu_edram4_alias_by_shape) 429`,
  `redirected clears straight into their consumer: 429`, `redirected clears into color owners: 2236 tiles`.
- Package 19 cleared one barrier pair per run of tiles. On the console that is
  `vkCmdPipelineBarrier call sites per Swap ... ReportCalls+0x10444: 393.1 (393.1 with MEMORY access)` out of
  `427.4 vkCmdPipelineBarrier calls ... 420.3 calls with MEMORY_READ / MEMORY_WRITE access`, plus
  `render passes per Swap: 258.2 begun ... depth ... clear 65.0` and `vkCmdClearAttachments 112.0`.
- Package 20 (`banjo_gpu_edram4_clear_merge`, T:5795-5866). PC v20B:
  `redirected clears merged ... 1500 clears, 54300 runs of tiles in 1500 passes and 1500 rectangles (3000 barriers
  instead of 108600); 0 took the old path`. Each clear is now **1 LOAD pass, 1 vkCmdClearAttachments and 2 full
  barriers** (`BarrierClearEDRAM4`, T:4092). Not measured on a console yet.
- What triggers it: every whole-surface Clear of the shadow maps (1024 in pitch 1040, 768 in 800), the 220x220 target
  and the small targets. Direct3D clears the whole tiles through a 4x alias of half the pitch and the edge strips at
  1x (`docs/d3d-native.md` §9). The Clear marker widens only the 1x strips. The alias rectangle is declined:
  `270 surface wider than the pitch` (console level) and `1200 surface wider than the pitch` (PC).

**M4. Colour halves of depth+colour clears (`banjo_gpu_edram4_clear_color0`, `_color_consumer`).**
- PC v20B per 10 s: `depth + color 0 clears redirected into their 1x owners: 232800 tiles in 600 clears`
  (2 a frame, about 390 tiles each).
- `CommitClearColor0EDRAM4` (T:6055) brackets its CLEAR passes with **another** pair of full barriers (T:6078, 6081),
  right after the merged depth half's pair.

**M5. Resolves through EDRAM (30 a frame).**
- Each resolve:
  - synchronises its source rectangle (`SynchronizeEDRAM4(*target_render, sync_area)`, T:1805; fast path now);
  - copies (colour on the 3D engine by `banjo_gpu_resolve_draw`; non-half depth by `vkCmdCopyImage` on the copy
    engine);
  - clears through the EDRAM range plan (`GetPlanClearedEDRAM4` T:4004, `PrepareRangeClearEDRAM4` T:4056).
    **Each clear rectangle gets its own CLEAR pass and a full barrier on each side** (colour T:1953-1960, depth
    T:2060-2070).
- The tiles-once spare-row clear (`ClearTilesOnce`, T:1618) uses **3 full barriers for 2 passes** (T:1645-1650).
- The Resolve marker already widens whole-surface clears over the padding (T:4018-4029; console:
  `resolve clears widened over their edge tiles' padding (cumulative): 12630`).
- `copies` = 4.7-5.2 ms a frame on the console, 1.0-1.4 on the PC.

**M6. Scene-depth resolves with a format conversion ("guestspace depth resolve").**
- 6 a frame on both machines (console level `guestspace depth resolve=516` per 86 Swaps; PC 1800 per 300). This is
  the scene depth `1DFD6000 1280x720`, resolved at 3 points per tile, for both tile halves (tiles once).
- `CopyDepth` (T:2131) calls `ResolverDepthGuestspaceEDRAM4` (T:2292 -> T:7759). That is a compute dispatch
  (`me_depth_resolve_guestspace_msaa2.comp`: `texelFetch(sample) * 2.0`) between two
  `ALL_COMMANDS`/`MEMORY_READ|WRITE` barriers (T:7815-7830).
- `EnsureCapacityConversionEDRAM4` (T:9749) files it under **`edram_alias`** (T:9758-9760): 1.9 ms console, 0.3-0.4 PC.
- This is the depth resolve any renderer must do. The emulation parts are the host encoding (half-range D32 -> guest
  value), the second dispatch for the second tile's rows, and the barriers.

**M7. Split tiles (`banjo_gpu_edram4_split_tiles`).** PC v20B: `3300 tiles taken without a transfer ... 3300 splits
ended by a whole overwrite ... 0 healed`, 11 a frame. Console level: 1459, 0 healed. CPU only. Leave as is.

**M8. Predicated tiling (tiles once).**
- Console level: `ring thread time in the first passes 2397.1 ms, in the later ones 269.6 ms` (90 Swaps), which is
  **3.0 ms a Swap**. Other reports: 214-350 ms per 81-164 Swaps (2.3-3.5 ms).
- PC: `1631.1 ms ... 171.2 ms` (300 Swaps).
- `second-pass draws dropped 39149 (each matched ...)`: the draws are already dropped. What remains is parsing the
  PM4, constants, shader loads and the per-draw comparison (S:4130-4141, `TilesSignature`).

**M9. CPU bookkeeping on the ring.**
- Console level, per 10 s: `EDRAM mode4 CPU bookkeeping since last report: sync 4620 calls / 1642120 tiles, publish
  189656 calls / 554048 tiles; O(1) fast paths: sync 10837, publish 186154`.
- `EDRAM draw memo ... 92.0 % from the memo`.
- Symbolized ring profile (level reports 48-52, 1017 samples):

  | Function | share |
  |---|---|
  | `TargetsVulkan::Draw` | 2.0 % (partly the inlined memo) |
  | `PublishEDRAM4` | 0.7 % |
  | `RedirectClearDepthEDRAM4` | 0.5 % |
  | `PrepareDrawEDRAM4` | 0.3 % |
  | `TileUsedEDRAM4` | 0.2 % |
  | others | 0.1 % each |

  Over reports 44-63 (4727 samples) it is the same: about 3.5 % of the ring. `CrcFingerprint` 14.8 % and
  `DrawsVulkanImpl::Draw` 14.8 % dwarf it.

**M10. Hidden cost: no ZCULL on 1x depth views.**
- `with_transfer_dst` (T:8599-8601) gives every 1x depth view TRANSFER_DST because of
  `masseffect_native_edram4_stencil_copy`. The size exemption `kSideShadows = 1600` (T:10994) is Mass Effect's shadow
  map, not Banjo's.
- Console `rex_stderr.log`:
  - `[nvk] ZCULL: SIN plano para profundidad 1040x1040: usos incompatibles (TRANSFER_DST?)`
  - the same for `800x800`, `1280x1280`, `560x720` and `1024x1024` / `768x768` / `512x512` (resolved textures).
- `renut_001.log`: `depth target base 000, format 0, 1040x1040 ...; ZCULL plane possible: no, 1x view whose stencil
  is imported by copies (TRANSFER_DST: masseffect_native_edram4_stencil_copy)`.
- The planes go to the 2x scene views (`plano de 65504 bytes para profundidad 1280x720`), but the scene uses reversed
  depth while `ZCULL mode: less (NVK_ZCULL)`. So ZCULL is effectively off everywhere. The shadow maps are the depth
  targets where "less" matches, and they are excluded.
- Shadow casters on the console: `#7 PS n-1 cat 3: 3.01 ms/frame, 596 draws, 0.000 Mfrag, ... 496493 vertices`.
  The "640" category is 4.5-6.8 ms a frame in the level.

**M11. Full barriers remaining after the clear merge (PC v20B, 14:41:29, 47.1 full flushes a Swap).** The call sites
come in pairs:

| Call sites | per Swap | Most likely source (matched by count per frame) |
|---|---|---|
| +0x4bb93 / +0x4bcd0 | 6 + 6 | guestspace depth resolve, before/after |
| +0x2b6f2 / +0x2b895 | 5 + 5 | merged redirected clears |
| +0x2e492 / +0x2e5b2 | 5 + 5 | per-rectangle resolve clears |
| +0x398ee / +0x3998d | 2 + 2 | `CommitClearColor0EDRAM4` |
| +0x3b382 / +0x3b41c, +0x75703 / +0x7580e | 2 + 2 each | |
| +0x3a56f / +0x3a629 | 2 + 2 | narrow; `BarrierBefore/AfterCopyResolve` (T:9403/9418) |
| +0x23570 / +0x23607 | 1 + 1 | |

The PC build's renut.pdb can confirm the mapping before step 2.

---

## 2. Each mechanism: can it go, exactly, and how

What the native Direct3D layer gives today:
- The Clear marker (`BJCL`: rectangle, colour target 0 and depth surface sizes, flags) and the Resolve marker (`BJRS`:
  whole-surface size) (`banjo_d3d_clear.h`; ring S:2014-2033, `WidenApiClear` S:3285).
- The device's bound surfaces at every draw (+12440 + 4i, +12456: the native draw path reads the device).
- The tiling entry points (`8227A9F0`, `8227B218`, `82278C08`, `docs/d3d-native.md` §1).

The layer does **not** yet tag surface identity (which D3D surface object a view belongs to) or tile-pass boundaries.

| Mechanism | Exactly removable? | What it needs | What broke before (do not repeat) |
|---|---|---|---|
| M1 steady transfers | already 0 | - | Widened clears must keep `banjo_clear_own_surface` (S:3339, T:5487), or the old owner keeps the tiles: +48 tiles a frame. A partial colour clear redirect gained nothing (the next draw pulled the same tiles). |
| M1 transition bursts | partly | A trace of the jigsaw/loading frames (`banjo_trace_frame_s`). They are 1-tile depth imports into the 560-pitch depth view at base 0 plus 2x->1x depth representation switches. | `single_tile` (forcing one predicated tile) put the post-process surfaces over EDRAM base 1440: bands. |
| M2 late stencil fetch | **yes**: the stencil bits are what they must be either way. The 3D masked-pass path (clear, then one pass per bit ever set, `edram4_bits_stencil`) writes the same bytes as the copy engine. That is the existing "9-pass" path, T:7662-7718. | Route stencil-only fetches to it. Fetch only the tiles the draw's area touches (`TileUsedEDRAM4` with `edram4_draw_area_`). If no stencil bit was ever set in the source (`bits_source == 0`), the fetch is a stencil clear to 0. | The copy path exists because 8 passes per import were slow for Mass Effect's big imports; it stays for those (see 1b). |
| M10 ZCULL on 1x depth | **yes**, by the Vulkan contract (ZCULL only rejects what the depth test rejects). Views without TRANSFER_DST already work everywhere: imports instead of tile copies (T:5151-5176), clears in passes (T:2098-2114, 9723), clipped-import heal for split tiles (T:4501, 4552, 7481). | Drop the stencil-copy reason from `with_transfer_dst` (T:8599). | Driver-dependent: NVK must keep ZCULL right across `vkCmdClearAttachments` in a LOAD pass (the clear merge's path) and across `gl_FragDepth` imports. "LOAD_ZCULL on garbage data kills the GPU context" (driver patch); the UNDEFINED->GENERAL barrier in `Prepare` must stay (T:9715-9723). |
| M3 redirected clears | the clears themselves are the game's clears (they stay); their **barriers** go. The render pass from `PassFor` already has `srcStage ALL_COMMANDS` with attachment and transfer writes at begin, and attachment writes -> shader/transfer/attachment at end (D:12118-12144). Compute writes into attachments end with their own full barrier. | Drop `BarrierClearEDRAM4` around `ClearRectsInPass` / `ClearColorInPass` when no compute batch is pending (`edram4_batch_.compute_pending`). Later: one "surface clear" per Clear marker (step 7). | Merge correctness was argued from NVK's clear code (D:7386-7388). |
| M4 colour halves | same as M3 | share one barrier pair with the depth half, or none | values near an 8-bit rounding tie are declined (keep) |
| M5 resolve clears | yes: the same rectangles, fewer passes | `ClearRectsInPass` with all rectangles of a plan; the tiles-once clear as one pass with colour + depth attachments | Missing bottom half came from resolve rows (`SearchResolvedPartial`); keep its rules. |
| M6 depth resolve | the conversion stays (the game samples the converted value); the second dispatch and the barriers go | Merge a tiles-once pair (the first tile's copy and `CopyTilesOnce`'s extension, issued back to back at S:4164) into one dispatch over the union, when both land in the same `Resolved` image with contiguous rows | "resolve hand-over" is impossible (pitch-wide views, 1280x1280 images); `masseffect_native_resolver_no_copy` crashed the PC within minutes |
| M7 split tiles | not needed: free on the GPU, no heals | - | - |
| M8 second tile pass | yes, with a state proof | Register file and constant/fetch/bool state at pass 2 entry equal to pass 1 entry, except the tile registers. IB sequence identical by address, size and fingerprint (the page bitmap exists). No GPU-written memory read inside the pass. Tiling markers from the D3D layer make the boundaries explicit. | Tiles-once hole (a first pass that begins with a resolve); records-instead-of-PM4 lost two console A/Bs (do not rebuild the transport). |
| M9 bookkeeping | yes, by surfaces (`docs/d3d-native.md` §9 step 3), but small (about 3.5 %) | surface identity markers | the Lock census: Direct3D object versions are **not** a proof for memory content |

---

## 3. Ranked plan

Project rules for every step: own key, master-switch group (`src/cpu/banjo_enhancements.cpp`), verify self-check,
docs (`docs/enhancements-reference.md` "EDRAM" or "Command recording", `docs/enhancements.md`), in-run A/B target
where possible. Targets 1-31 are taken (S:310-322). New ones start at 32: extend `.range(1, 31)` and the label chain
at S:4422-4438. PC test = `bash $SP/run_console.sh <name> <s> [args]` (console toml + intro route).

### Zero-code probes for the next console run (do these first; configs only)

- `configs/banjo_stencil_fetch_3d_probe.toml`: `masseffect_native_edram4_stencil_copy_min_tiles = 2048`. All stencil
  imports, the late fetch included, take the 3D masked passes; TRANSFER_DST stays. Expect `copy-engine stencil
  imports 0` and `edram_import9` 1.3 -> about 0.1-0.3 ms.
- `configs/banjo_depth1x_zcull_probe.toml`: `masseffect_native_edram4_stencil_copy = false`. The same, plus every 1x
  depth view loses TRANSFER_DST: `ZCULL plane possible: yes` for 1040x1040 / 800x800, and `[nvk] ZCULL: plano ...
  1040x1040` in `rex_stderr.log`. Watch the "640" category and **look at the shadows**.
- `banjo_gpu_edram4_clear_merge` on against off (`banjo_ab_target = 28`, `banjo_direct_state_ab_s = 4`) gives the
  console's price of about 370 full flushes in one run (`GPU A/B` line). That calibrates step 2.

Both stencil keys are exact either way: the 9-pass path and the copy path import the same bits.

### Step 1a: stencil-only fetches in the 3D engine, limited to the draw's tiles (GPU, about 1.1 ms a frame)

- **Change.**
  - In `FetchStencilDeferredEDRAM4` (T:6205), when the new key is on, set a flag that makes `copy_stencil` false
    (T:7646). The fetch then clears the stencil of the tile-row runs (T:7665-7681) and draws one masked pass per bit
    set in `source.edram4_bits_stencil`.
  - When `bits_source == 0` (T:7697), skip the import pass and its descriptor set entirely: record only the stencil
    clear (`ClearRectsInPass` with `VK_IMAGE_ASPECT_STENCIL_BIT`, value 0, a LOAD pass).
  - At T:6497, pass the draw's tile range instead of `[0, plan.length_tiles[0])`: the tiles where
    `TileUsedEDRAM4(*image, t, RasterArea(*image, edram4_draw_area_))`. Untouched tiles keep their deferred record
    for whoever uses them next.
  - Add the diagnostic first: a per-pair counter like `edram4_import9_pairs_`, "late stencil fetch
    `<source>-><target>`, tiles, VS/PS/dc of the draw", printed in the 10 s report. That names the 96-tile case.
- **Why exact.** The masked-pass import is the original stencil path. Each pass sets bit k where the source's bit k is
  set, after a clear to 0. Bits never set in the source are 0 everywhere, so skipping them, or the whole import, writes
  the same bytes. Tiles outside the draw's area are not read by this draw. Their record stays and is fetched by the
  first draw that touches them, exactly as now.
- **Self-check (verify mode).** For the first 4096 fetches, and every one with `banjo_cpu_verify`:
  - run `me_edram_stencil_to_buffer.comp` over the source tiles (expected) and over the target tiles after the
    fetch (actual, through the target's stencil view);
  - compare the two buffers in a small compute dispatch that increments a counter in a host-visible buffer;
  - read the counter at the slot's fence. Non-zero means `DIFFERENCE` and the key turns off for the session.
  - Also count fetched tiles that lie outside the draw's area (must be 0).
- **Key.** `banjo_gpu_stencil_fetch_3d` (non-CPU), A/B target 32 (`AbOn(32)` in `FetchStencilDeferredEDRAM4`).
  Report line: "late stencil fetches: N in the 3D engine (M as a clear only), K tiles; copy engine 0".
- **Risk.** Low. Only the destination of an existing path changes.
- **PC test.**
  - `run_console.sh sf3d 340` (counters: `copy-engine stencil imports 0`, the new line, GPU A/B import9).
  - `run_console.sh sf3d_ab 345 --banjo_direct_state_ab_s=2 --banjo_ab_target=32 --banjo_record_s=120
    --banjo_record_seconds=30 --banjo_record_name=latest`, then `tools/ab_flicker.py`. The late fetches run from 96
    s to 225 s in this route.
  - `run_console.sh sf3d_verify 340 --banjo_cpu_verify=true` (`differences: 0`).

### Step 1b: 1x depth views without TRANSFER_DST, so they get ZCULL (GPU, unknown, up to about 1-2 ms)

- **Change.** At T:8599, with `banjo_gpu_depth1x_zcull` on, drop the term
  `(REXCVAR_GET(masseffect_native_edram4_stencil_copy) && msaa_edram_actual_ == 0)`. `copy_stencil` already requires
  `target.accepts_target_of_copy` (T:7649), so imports into these views take the masked passes. Same-format
  depth->depth tile copies take `ImportColorDepthEDRAM4` (T:5173-5176). Split heals use the clipped import
  (`edram4_import_clip_`). Update the creation log line (T:8626-8633).
- **Why exact.** ZCULL rejects only fragments the depth test rejects. The code already runs the 2x scene views
  without TRANSFER_DST (clears in passes, imports, clipped heals). Nothing reads padding through these views (the
  Clear operation's assumption, poison-tested).
- **Self-check.**
  - Creation: log every depth view's usage and whether the driver reported a plane.
  - Runtime: the step 1a verify compare covers every import into these views.
  - ZCULL itself cannot be self-checked. Use picture checks on the console: shadows at the flips of two configs,
    plus `stills_ab.py` / `vidcmp.py` on two recordings of the same scripted stretch.
- **Key.** `banjo_gpu_depth1x_zcull` (non-CPU). **No in-run A/B** (image creation flags). Use two configs, like
  deferred recording before it became switchable, plus `configs/banjo_depth1x_zcull_ab_pair.toml` / `..._off.toml`.
- **Risk.** Medium, and console only.
  - NVK must keep the plane right through `vkCmdClearAttachments` in LOAD passes (redirected clears into the shadow
    maps after the clear merge) and through `gl_FragDepth` imports. If shadows show holes or flicker only on the
    console, this key is the cause.
  - Optional safety: for views with a plane, clear single rectangles covering both aspects with a `kLoadClear` pass
    (`ClearDepthInPass` with an area) instead of `ClearRectsInPass`. Package 19's per-run clears did that.
- **PC test.** `run_console.sh z1x 340 --banjo_gpu_depth1x_zcull=true` against `... =false`: 0 `DIFFERENCE` / `REJECT`;
  `sync cache ... transferred` unchanged in transitions (imports instead of copies); verify run 0 differences;
  recordings of both compared with `tools/match_frames.py`.

### Step 2: remove or narrow the full barriers around EDRAM operations (GPU, about 0.5-1.5 ms)

- **Change** (key `banjo_gpu_edram4_lean_barriers`).
  - Before every `BarrierClearEDRAM4()` that directly brackets a `ClearRectsInPass` / `ClearColorInPass` /
    `ClearDepthInPass` / `ClearOnlyDepthInPass`, call `CloseBatchEDRAM4()` if a compute batch is pending, then skip the
    barrier. That covers:
    - merged redirected clear T:5834 / 5852;
    - `CommitClearColor0EDRAM4` T:6078 / 6081;
    - SDK colour clear T:1956 / 1959;
    - SDK depth clear T:2062 / 2069;
    - `ClearTilesOnce` T:1645 / 1647 / 1650;
    - `RedirectClearStencilEDRAM4` T:6187 / 6190.

    The render pass's own dependencies (D:12118-12144) order these clears against earlier attachment and transfer
    writes, against all earlier commands (srcStage ALL_COMMANDS), and against later shader, transfer and attachment
    access.
  - Narrow the guestspace resolve's barriers (T:7815-7830):
    - before: `src = DEPTH_STENCIL_ATTACHMENT_WRITE | TRANSFER_WRITE | SHADER_WRITE` at
      `LATE_FRAGMENT_TESTS | TRANSFER | COMPUTE_SHADER | FRAGMENT_SHADER | VERTEX_SHADER` (the last two are for
      earlier sampling of the destination), `dst = SHADER_READ | SHADER_WRITE` at `COMPUTE_SHADER`;
    - after: `COMPUTE_SHADER SHADER_WRITE -> VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER | TRANSFER`,
      `SHADER_READ | TRANSFER_READ | TRANSFER_WRITE | SHADER_WRITE`.
  - The same narrowing for the "after" barrier of `CopyStencilEDRAM4` (T:7420-7424) and `ConvertDepthEDRAM4`
    (T:7905-7909).

  This is the same idea as `banjo_gpu_pass_narrow_end`, which already removed NVK's constant/QMD cache invalidations
  at pass ends.
- **Why exact.** The same ordering and visibility for every read and write that exists. The change is in what is
  flushed and invalidated, not in what is drawn. The PC result of the clear merge (busy GPU -2.7 ms) shows how
  much the redundant ones cost even on NVIDIA.
- **Self-check.** One PC run with the Khronos validation layer's **synchronization validation** (`VK_LAYER_PATH` +
  `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` + `VK_KHRONOS_VALIDATION_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`,
  the same env route RenderDoc uses). It must report no new hazard against a run with the key off. At runtime, count
  per call site in `banjo_diag_vk_calls` (`barriers per Swap ... with MEMORY_READ / MEMORY_WRITE access` should fall
  from about 47-62 to about 5-10).
- **Key.** `banjo_gpu_edram4_lean_barriers` (non-CPU), A/B target 33.
- **Risk.** Low-medium. A missed hazard shows up as flicker, possibly on NVK only. Sync validation plus the console
  A/B recording cover it.
- **PC test.**
  - `run_console.sh lean 340` (barrier lines, `GPU A/B`);
  - `run_console.sh lean_ab 345 --banjo_direct_state_ab_s=2 --banjo_ab_target=33 --banjo_record_s=120
    --banjo_record_seconds=30 --banjo_record_name=latest` + `tools/ab_flicker.py`;
  - one sync-validation run of about 150 s.

### Step 3: one depth-resolve dispatch per tiles-once pair (GPU, about 0.3-0.7 ms of 1.9)

- **Change.** At S:4164 the ring calls `CopyTilesOnce` right after the first tile's `Copy`. For a depth source
  (`source >= 4`) with a `depth_float24_half` view, let `CopyDepth` take the extension rectangle up front. Add
  `RegistersCopy::tiles_once_union_rows` (set by the ring when `t.wide` and the extension rectangle is "the next rows,
  same columns"). The first call then dispatches over the union of source rows `[y0, rect[3])` into the destination
  rows `[dy, dy + height)`, and `CopyTilesOnce` for that resolve only reports the base.
  - Conditions: same `Resolved` image (`SearchResolvedPartial` gives the same parent and the rows continue), same
    `sample_select`, no clear between the two copies. The clear runs after the copy in `CopyInternal`, and
    `CopyTilesOnce` clears nothing (T:1606, `extension.rb_copy_control &= ~0x300u`).
  - Otherwise use the old two dispatches.
  - Combine with step 2's narrowed barriers. Optionally, as a separate key, a fragment-shader version
    (`banjo_gpu_depth_resolve_draw`, R32F attachment, `texelFetch(ms, sample) * 2.0`) that keeps the work on the 3D
    engine like `banjo_gpu_resolve_draw`.
- **Why exact.** The shader is a per-pixel function of (source pixel, sample) with no neighbourhood. The union
  covers exactly the pixels of both copies, at the same destination texels.
- **Self-check (verify mode).** Read back the destination rectangle after the merged dispatch for the first N
  resolves, and compare it with a two-dispatch run into a scratch R32 image. Mismatch means `DIFFERENCE`.
- **Key.** `banjo_gpu_depth_resolve_merge` (non-CPU), A/B target 34. Report: "depth resolves merged: N pairs, M
  dispatches saved". Move this work from `edram_alias` into `copies` or a new `depth_resolve` category, so the EDRAM
  categories only show emulation (see section 4).
- **Risk.** Low.
- **PC test.** `run_console.sh drm 340` (`guestspace depth resolve` 1800 -> 900 a report) + the A/B recording (water
  and soft particles sample this texture).

### Step 4: skip the second tile pass's command buffers (CPU, 2.3-3.5 ms a Swap of ring at stock)

- **Change.**
  - Snapshot at pass 1 entry and pass 2 entry: the register file, ALU/fetch/bool/loop constants, and bound shader
    slots.
  - Record pass 1's IB list (address, size, fingerprint from the command-buffer page bitmap) and pass 1's final
    state.
  - If pass 2's IB list matches, its entry snapshot equals pass 1's apart from the tile registers (bin select/mask,
    `PA_SC_WINDOW_OFFSET`, window scissor, `RB_COPY_DEST_BASE`), and pass 1 had none of {MEM_WRITE, REG_TO_MEM,
    COND_WRITE, EVENT_WRITE_SHD/ZPD with data, WAIT_REG_MEM on GPU-written memory, memexport draws not predicated to
    one tile}, then:
    - do not run pass 2's IBs;
    - install pass 1's final state with pass 2's tile registers;
    - run pass 2's resolves (dropped already, they only advance `resolves_compared`) and its clears as now.
  - Better with D3D tiling markers (hook `8227A9F0` / `8227B218` / `82278C08`): `[BJTB tile k]` / `[BJTE]`.
- **Why exact.** The second pass's draws are already dropped after a field-by-field match. With identical entry
  state and identical bytes, every packet of pass 2 computes the same register values as pass 1, apart from the tile
  registers, which are restored.
- **Self-check.** Verify mode runs pass 2 anyway and compares its final state and draw signatures with the skip's
  prediction. Any difference means `DIFFERENCE` and the key goes off for the session.
- **Key.** `banjo_ring_tiles_once_skip` (CPU), A/B target 35 (`ring thread A/B` line).
- **Risk.** Medium-high. This is "a proof that the state they start from is the first pass's"
  (`docs/enhancements.md`, render round of 2026-10-14). Watch the tiles-once hole (a first pass that begins with a
  resolve) and draws "for the second tile alone".
- **PC test.** `run_console.sh tskip 340` (ring time in later passes -> about 0), verify run, A/B recording at the
  intro (seam at row 384).

### Step 5: resolve clears and the tiles-once clear, one pass per image (GPU, about 0.2-0.5 ms)

- **Change.**
  - In `CopyInternal` (T:1952-1961) and `ClearDepth` (T:2059-2071), with the key on, record all of a plan's
    rectangles with one `ClearRectsInPass` (colour, or depth+stencil) instead of a pass and two barriers each.
  - In `ClearTilesOnce` (T:1644-1650), clear colour and depth in one LOAD pass with both attachments (both images
    are `plain` and the same size by `TilesOnceImages`).
- **Why exact.** The same rectangles and values. This is `banjo_gpu_edram4_clear_merge`'s argument.
- **Self-check.** Verify mode compares a readback of one cleared rectangle per image against the value
  (cheap: 1 texel per rectangle corner), for the first N.
- **Key.** `banjo_gpu_resolve_clear_merge` (non-CPU), A/B target 36. It can share the PC runs with step 2.
- **Risk.** Low.
- **ZCULL caveat.** The scene's 2x views have ZCULL planes, and package 19 cleared them with `loadOp = CLEAR`. Keep
  `ClearDepthInPass` (`kLoadClear`) for depth images without TRANSFER_DST, and merge colour only, until the console
  confirms that `vkCmdClearAttachments` keeps ZCULL right.

### Step 6: loading-transition representation switches (hitches, trace first)

Record a `banjo_trace_frame_s` frame inside the jigsaw transition (console toml, intro route, the transition at about
150-170 s on the PC: `depth import=1722 ... 11520 transferred`). Then classify:
- `draw/depth-repr-switch` (960 a report);
- the 1-tile imports into `000:560x720`;
- the 512-tile copy-engine stencil imports.

The likely tools are the Clear marker and split tiles extended to depth views at that pitch, and step 1a/1b for the
stencil part. Keys follow from the findings.

### Step 7: surface-level Clear and per-view memo epochs (CPU, up to about 3 % of the ring)

- A Clear between `BJCL` and `BJCE` becomes one operation:
  - all tiles of the surface's range go to the surface's own view, cleared to the value from the proof;
  - the alias draw, the redirect's per-tile `goal()`, the learned-consumer maps and the colour-half runs are no
    longer needed.

  Add the surface object addresses (device +12440 / +12456) to the marker so the view is known even without a 1x
  strip.
- Per-view epochs instead of the global `edram4_epoch_`. Each reassign now invalidates every view's memo: 8058
  full-path draws per 10 s in the level.
- Last, because the measured gain is small and the risk is the consumer cases (the 2x scene depth read as 1x).

### Not recommended (past failures or not exact)

- One predicated tile at the Direct3D level: bands at the post-process bases.
- `masseffect_native_resolver_no_copy`: crash.
- Resolve hand-over (texture = target image): pitch-wide, 1280x1280 images.
- Records instead of PM4: two lost console A/Bs.
- Write-watch of command or vertex pages: NEON corruption on the console.
- Lock-based proofs: the Lock census found writes without Lock.

---

## 4. Correctness risks and leftovers found in the code

1. **Every 1x depth view loses ZCULL to an almost unused path** (M10, T:8599-8601). The copy-engine stencil import
   runs once a frame in steady state, yet it costs both shadow maps their ZCULL plane every frame.
2. **The copy-engine stencil-only fetch records an empty render pass** (`steps = 0`, T:7661). Before it come a
   descriptor-set allocation, viewport/scissor/descriptor binds, and `EnsureCapacityConversionEDRAM4("depth import")`,
   which ends the open render pass (T:9754). Then come three engine switches and a full barrier (T:7420-7424). That is
   1.3 ms a frame at stock for 96 tiles.
3. **The late fetch covers `[0, plan.length_tiles[0])`** (T:6497), not the tiles the draw touches. It is exact but
   can fetch more than needed.
4. **Redundant full barriers next to render passes that already order the same work**: T:1645-1650 (3 for 2 passes),
   1956/1959 and 2062/2069 (2 per rectangle), 5834/5852, 6078/6081 (another pair right after the merged depth half).
5. **ZCULL with `vkCmdClearAttachments` (package 20, console).** The clear merge now clears depth with LOAD +
   `vkCmdClearAttachments` where package 19 used `loadOp = CLEAR` passes for depth+stencil runs: 65 depth CLEAR
   passes a frame on the console. ClearDepth's own comment (T:2098-2105) says that only a `loadOp = CLEAR` resets the
   ZCULL hi-Z. If a merged redirected clear ever lands in a view with a plane (a 2x scene view cleared in place, T:5633,
   "a depth owner with the same encoding"), and NVK does not update ZCULL on `ClearAttachments`, the console could
   cull against stale data. The PC (NVIDIA) cannot show this. **Check the next console run's scene for missing
   geometry with the clear merge on and off (A/B 28).** The safe variant is to use `ClearDepthInPass(area)` for owners
   without TRANSFER_DST.
6. **"Single-sample approximation" exports.** `ConvertDepthEDRAM4` (T:7834) logs every 2x-source depth->colour export
   as `(single-sample approximation)` (T:7912-7917). These still run in the loading transition
   (`draw/depth->color=480ops/480tiles`, console 13:20:05) and once at start-up (720 tiles). They are not proven exact.
   This is pre-existing; worth a trace in step 6.
7. **Misleading GPU category.** The guestspace depth resolve, a real resolve, is counted as `edram_alias`
   (T:9758-9760). So the "EDRAM" categories never reach 0 even with no emulation left. Give it its own category or
   `copies`.
8. **`renut.console.toml` has drifted from `dist/switch/banjo-nx/renut.toml`.** It lacks `banjo_d3d_api_clear`,
   `banjo_d3d_api_resolve`, `banjo_gpu_early_alpha`, `banjo_gpu_skip_dead_draws`, `banjo_ring_pmull_fingerprint` and
   `banjo_ring_vertex_lock_rule = false`. The code defaults equal the shipped values today, so the PC runs were right.
   The note says the file must equal the shipped toml plus the PC keys, so re-copy it.
9. **Known and accepted (documented, not a new risk).** Tiles once is not bit-exact for a draw that samples the
   resolved scene across row 384 (S:4689-4690).
10. **Tiles once ring cost.** The second pass's ring time is still 11-12 % of the first pass's (M8). It is the largest
    EDRAM-shaped CPU item.

---

## 5. Order of work and what to measure

1. Ship configs for the zero-code probes (stencil copy threshold, stencil copy off, clear merge A/B 28) with the next
   package. Read `edram_import9`, "640", `GPU A/B`, `copy-engine stencil imports`, and `rex_stderr.log` ZCULL lines,
   and look at the shadows.
2. Step 1a (with the late-fetch pair diagnostic), then 1b behind its key.
3. Step 2 and step 5 together: the same code places, one sync-validation PC run.
4. Step 3.
5. Step 4 (CPU), once the GPU items are in. In the intro the ring is the limit at stock.
6. Steps 6 and 7 as time allows.

For each PC run, check:
- `DIFFERENCE/MISMATCH` = 0, `REJECT` unchanged;
- `sync cache: 0 transferred` in steady state;
- the step's own report line;
- `barriers per Swap` / `render passes per Swap` (`banjo_diag_vk_calls`);
- the `GPU A/B` line;
- an `ab_flicker.py` result around the flips, with recordings in the 96-225 s window where the late fetch and the
  shadow-map clears run.
