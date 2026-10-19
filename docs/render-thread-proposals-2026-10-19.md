# Render-thread CPU proposals of 2026-10-19 (package 34 console profile)

Twenty-four proposals to cut the CPU time of Renut's render thread on the console (Cortex-A57, 1020 MHz, deko3d back
end), ranked by the saving expected there, plus the ideas looked at and rejected. Read-only research: nothing in the
project was changed, built or run. Every proposal keeps the pictures and the game's behaviour identical; each says why,
and what check would catch a mistake.

Already done, measured or rejected, so not proposed again (docs/cpu-codegen-proposals-2026-10-19.md section 0,
docs/deko3d-renderer.md sections 13-15, docs/renut-renderer-port.md, docs/enhancements.md): the store cards for cached
vertex and index copies, the texture-slot / render-target / pipeline / `NewOp` memos as built in package 34, latched
switches, the plume group diffing, the clean-only upload ring, pooled clears and resolves, the record worker (Vulkan
only), a faster stream hash, hashing on another core, a per-frame stream proof, write-combined upload memory (measured
slower: 2.5 against 5.2 GB/s), MSAA or resolution changes. Work in progress in the source while this was written
(17:00-17:10, not proposed here): plume's barrier merging and `banjo_deko3d_barrier_lean`, and the GPU profile timing
one frame in four with the per-shader breakdown (`gpu_profile.cpp` `kShaderSample`).

Line numbers are those of the source at 17:10 on 2026-10-19; the samples are of the package 34 NRO.

## 1. Where the render thread's time goes

**Source.** `builds/2026-10-19_1614_round34_cpu_gpu/.../logs/rex/rex_profile.log`, symbolized with the exact ELF of that
NRO (`scratchpad/pkg34_elf/renut_pkg34.elf`; the sampled PCs land on instructions of the expected functions, e.g. the
hottest `PmullHash` PC is the `ldp q21, q23` of its 64-byte loop at 0x23f5328, the hottest `armDCacheClean` PC the
`dc cvac` at 0x2f42e04). Render thread = thread 1227, priority 0x2C, core 1. Windows 4-23 (from the level entry on,
17.8-35.3 fps): 7,738 samples, 4,634 busy (60 %), 40 % waiting in the kernel (GPU fences, queue). The pc histogram is
complete; the `lr` view (top 40 per window) gives the caller of leaf callees; the `stack` view (top 30 stacks per
window, 78 % of the busy weight, scaled) gives inclusive time. Scripts: scratchpad `rt_research/` (`prof.py` parser and
symbolizer, `selfinc.py` self/inclusive, `callsites.py` call sites from `lr`, `annot.py` sampled instructions,
`regions.py` samples between calls).

**Units.** RT % = share of the render thread's busy samples. Gameplay busy time is 25.1 ms a frame, so **1 RT % ≈
0.25 ms a frame**. In the GPU-bound windows (GPU 30-65 ms a frame) the render thread waits for the GPU and a saving shows
as idle time; in the CPU-bound ones (the intro, light scenes, level entry, where all three cores reach 265 % of 300 %)
it shows as frames.

**Per-frame facts** (`logs/renut_00*.log`, gameplay windows): 850-1,500 draws; 450-1,050 pipeline binds (the pipeline
changes at ~70 % of draws) and 280-720 state binds; 150-740 draws read fetch data through the address window; 8 kicks
(8 store-card intervals); SharedConstants reused by 14-35 % of draws; ~1,000 copies proven by their cards. libnx
counters (`rex_profile.log`): `armDCacheClean` 8-11 MB a frame cleaned at the submit, 0.7-0.9 ms.

### Time by area

| Area | RT % | ms a frame | What |
|---|---|---|---|
| Textures: content checks | 10.4 | 2.6 | `ContentHash` of linear textures (PmullHash 7.4), `SampleHash` of tiled ones (gather `memcpy` 2.5, hash 0.5) |
| Textures: binding | ~9 | ~2.3 | `BindTextureLocked` 4.1, `SamplerSlotLocked` 1.4, `FromHeader` 0.8, `TextureSlot` 0.4, `FromGuest` 0.3, the `FillTextures` loop in `DrawStateLocked` ~2 |
| Textures: uploads | ~2 | ~0.5 | `ReadLevel` + its `memcpy` 1.5, `DecodeBlockRow`, `TiledOffset2D` |
| Constants and the upload ring | ~17 | ~4.3 | `UploadFloatConstants` 6.5, SharedConstants `memset`/`memcmp`/`memcpy`/`Upload` 4.5, `armDCacheClean` 2.8, UBO rebinds (`bindSetBuffers`, `setGraphicsDescriptorSetDynamic`, `bindGraphicsBuffers`) 2.4, `Allocate` 0.6 |
| Memexport bookkeeping | ~11 | ~2.8 | `DrawNeedsExportFlush` 3.9, `ConvertExportedStreamsLocked` 2.5, `GatherMemExportStreams` 1.7, `TextureGuestExtent` 1.4 (+ its TLS call), `PendingExportSlots` 0.7, `MemExportBufferLocked` 0.4 |
| Geometry | ~14 | ~3.5 | `BindStreams` 3.4, `PrepareGeometry` 2.7, `CardsProve` 1.7, fetch-data `memcpy` at the kick 2.0, `UploadVertexFetchData` 0.9, `GetStreamSource` 0.8, conversions 1.6 |
| Pipeline | ~7 | ~1.8 | `MakeKey` 1.0, `GetPipelineSlow` 1.0 + XXH3 0.5 + `memcmp` 1.1, `OutputTrimMask` 0.5, plume `setPipeline` 1.4, `dkCmdBufBindShaders` 0.7 |
| Targets and viewport | ~4.5 | ~1.1 | `GetHostSurfaceSlow` 1.0, `GetHostSurfaceLocked` 0.7, `GetFramebufferLocked` 0.9, `ComputeViewport` 0.7, `BindTargets`, `GatherTargets` |
| GPU-profile diagnostics (on in renut.toml) | ~2.5 | ~0.6 | `ShaderProfileLabel` 2.0, timestamps, query results |
| The rest | ~20 | ~5 | `DrawStateLocked`'s own work, `Replay` 2.5 + `ApplyState` copies 0.7, plume draws, shader lookups, locks, malloc |

### Top functions

| Function | Self RT % | Self ms | Inclusive RT % | Inclusive ms |
|---|---|---|---|---|
| `render_thread::Execute` (all work) | 0.1 | - | 94.8 | 23.8 |
| `Replay` (command-buffer draws) | 2.50 | 0.63 | ~70 | ~17.7 |
| `DrawStateLocked` | 8.07 | 2.03 | ~77 | ~19.3 |
| `QueueCapture` closure (immediate draws) | 0.22 | 0.06 | 17.6 | 4.4 |
| `BindTextureLocked` | 4.14 | 1.04 | 18.6 | 4.66 |
| `memcpy` (all callers) | 10.01 | 2.51 | - | - |
| `ContentHash` (linear textures) | 0.0 | - | 9.4 | 2.37 |
| `PmullHash` | 8.03 | 2.02 | 8.0 | 2.02 |
| `UploadFloatConstants` | 6.50 | 1.63 | ~7.0 | ~1.75 |
| `DrawNeedsExportFlush` | 3.88 | 0.97 | 5.5 | 1.38 |
| `PresentLocked` / `SubmitAndAdvance` | 0.0 | - | 5.2 / 4.7 | 1.32 / 1.17 |
| `UploadRing::Publish` (cache clean) | 0.0 | - | 3.6 | 0.90 |
| `QueueKickFlush` closure (fills at the kick) | 0.0 | - | 4.3 | 1.08 |
| `BindStreams` (+ `GetStreamSource`) | 3.43 | 0.86 | ~4.6 | ~1.15 |
| `PrepareGeometry` | 2.68 | 0.67 | 3.8 | 0.96 |
| `GetPipelineLocked` | 0.67 | 0.17 | 3.9 | 0.97 |
| `armDCacheClean_L0` | 2.83 | 0.71 | 2.8 | 0.71 |
| `ConvertExportedStreamsLocked` | 2.46 | 0.62 | 2.5 | 0.62 |
| `ShaderProfileLabel` | 2.01 | 0.50 | ~2.3 | ~0.58 |
| plume `drawIndexedInstanced` | 1.25 | 0.31 | 2.8 | 0.71 |
| `GatherMemExportStreams` | 1.70 | 0.43 | 1.7 | 0.43 |
| `CardsProve` | 1.70 | 0.43 | 1.7 | 0.43 |
| `SamplerSlotLocked` / `TextureGuestExtent` | 1.42 / 1.42 | 0.36 / 0.36 | - | - |
| plume `setPipeline` | 1.36 | 0.34 | 1.8 | 0.46 |

### Shared leaf functions by caller (from the `lr` of their samples)

| Leaf | RT % | Callers |
|---|---|---|
| `memcpy` | 10.0 | `BindTextureLocked` (SampleHash gather) 25 %, `FlushVertexFillsLocked` (fetch-data copies) 20 %, `UploadRing::Upload` (SharedConstants, its only caller) 20 %, `ReadLevel` 12 %, `CompleteMemExportsLocked` 8 %, `Replay` (`ApplyState`) 7 %, `DrawStateLocked` 6 %, `ConvertVertices` 2.5 % |
| `PmullHash` | 8.0 | `ContentHash` 93 %, `BindTextureLocked` (SampleHash) 6.5 %, stream tracking 0.8 %. The stream checks' hashing is gone (package 34's cards) |
| `memcmp` | 2.3 | `DrawStateLocked` (SharedConstants dedupe) 51 %, `GetPipelineLocked` 31 %, `GetPipelineSlow` 18 % |
| `memset` | 0.8 | `DrawStateLocked` 96 % (the SharedConstants clear) |
| `XXH3_64bits` | 0.5 | `GetPipelineSlow` 100 % |
| `armDCacheClean` | 2.8 | `UploadRing::Publish` at the submit |

## 2. The proposals, ranked

| # | Proposal | Kind | Saving on the console | Effort | Risk |
|---|---|---|---|---|---|
| 1 | Store cards for the texture content checks (linear `ContentHash`, tiled `SampleHash`) and for the per-frame re-uploads of "dynamic" linear textures | render | -8..-10 RT % (2.0-2.5 ms) | medium | low (verify mode) |
| 2 | Float constants as pipelined constant-buffer updates of the changed groups (deko3d `dkCmdBufPushConstants` into one UBO per stage) | render + plume | -4.5..-6 RT % (1.1-1.5 ms) | medium | medium (GPU cost to measure) |
| 3 | Texture-binding memo keyed by texture, and `g_texture_epoch` bumped only when a binding can change (**bug**: bumped by every upload and every resolve; the per-slot memo almost never hits) | render, bug fix | -4..-5.5 RT % (1.0-1.4 ms) | small-medium | low (compare mode) |
| 4 | Memexport bookkeeping in one pass with per-shader and per-layout summaries (**bugs**: a 1.8 KB array zeroed per draw, 4 `GatherMemExportStreams` per draw, a 40 KB TLS memo) | render, bug fix | -4..-5 RT % (1.0-1.25 ms) | medium | low |
| 5 | SharedConstants built in place with line tracking (no memset / memcmp / shadow copy), and on deko3d pushed by changed lines | render + plume | -2..-4 RT % (0.5-1.0 ms) | medium | low-medium |
| 6 | Pipeline memo sized to the frame's pipelines (64 → 1,024 entries, 512 sets of 2 ways), and `OutputTrimMask` memoised | render | -2..-2.5 RT % (0.5-0.6 ms) | small | low |
| 7 | GPU-profile labels only in profiled frames, and memoised per shader record (**bug**: computed every draw while the profile now times 1 frame in 4) | diagnostics | -1.8..-2.2 RT % (0.45-0.55 ms) while on | small | very low |
| 8 | Targets path: framebuffer lookup by last key (**bug**: a linear search per draw), surface memo index hashed, viewport memo | render | -1.5..-2 RT % (0.4-0.5 ms) | small | low |
| 9 | Stream and index memo lines that hold the key and the bind data (one cache line instead of three dependent misses), and a per-page card memo per interval | render | -1.5..-2 RT % (0.4-0.5 ms) | small-medium | low |
| 10 | Upload ring: clean only the lines written (**bug**: fetch-data allocations reserve and clean space for copies they reuse), float-constant allocations sized to the registers read | render | -1..-1.5 RT % (0.25-0.4 ms); less after 2 | small | low |
| 11 | One-load globals: guest memory bases cached (a 4-load chain at 603 sites), `Latch()` inline (7 out-of-line calls per draw), hidden visibility for hot globals, trivial accessors inline | render, bug fix | -1..-2 RT % (0.25-0.5 ms) | small | very low |
| 12 | Per-shader summaries in `ShaderRecord` (used constant groups, valid masks, export flag, fetch masks): no per-draw `UsedGroups` loop and no cold `ShaderCacheEntry` lines | render | -1..-1.5 RT % (0.25-0.4 ms) | small | very low |
| 13 | plume deko3d per draw: direct UBO binding for the three constant sets, statistics without atomic read-modify-writes (**bug**: two `ldxr/stxr` loops per draw remain) | plume | -1..-1.5 RT % (0.25-0.4 ms) | small | low |
| 14 | Persistent fetch-data copies proven by the store cards (as the vertex streams) | render | -1..-2 RT % (measure the share first) | medium | low-medium |
| 15 | Immediate draws as deltas against a running device (package 34's proposal 3c, render-thread side) | render + guest side | -1..-2 RT %, guest -2..-3 % of a core | medium | low-medium |
| 16 | Prefetch the draw's records at `DrawStateLocked` entry | render | -1..-3 RT % (speculative) | small | very low |
| 17 | DC ZVA before whole-line writes into the upload ring (if 2 and 5 are not taken) | render | -1..-1.5 RT % | small | low |
| 18 | Upload-ring chunk churn and `makeBlock` logging off the render thread (**bug**: 32 MB chunks trimmed and remade mid-frame, with a heap walk and an SD flush) | render + plume | -0.3..-0.5 RT % average, and the hitches | small | very low |
| 19 | Render loop: `State().mutex` once per batch of items, one clock read per item | render | -0.3..-0.5 RT % | small | low |
| 20 | No heap allocation per draw: a flat table for `g_queued_streams`, scratch vectors for `ExpandIndices` and clears | render | -0.3..-0.5 RT % | small | very low |
| 21 | `DescribeBoundFetches` and shader-record lookups memoised per (vertex shader, declaration) | render | -0.3..-0.5 RT % | small | low |
| 22 | Smaller items: `SwapEndian` of the bools inline, `CountDrawKind` folded, `ResolveTraceOn` inline, `TextureFrameLocked` inline | render | -0.3..-0.5 RT % | small | very low |
| 23 | The submit's cache clean on a helper core, by ranges at each kick | render (moved) | moves 0.4-0.9 ms off the render thread | small-medium | low |
| 24 | Kick-time fills (vertex conversions, fetch copies, texture reads) on a helper core | render (moved) | moves 0.5-1 ms off the render thread | medium-large | medium |

Proposals 1, 3, 4, 6, 7, 8 and 11 are independent of each other and of the GPU work in progress; their savings add. 2
and 5 replace most of 10 and 17. 23 and 24 move work rather than remove it: worth it only where the render thread is
the frame's limit and core 2 has room (gameplay CPU total 125-170 % of 300 %).

---

## 3. The proposals

### 1. Store cards for the texture content checks and the dynamic re-uploads

**Kind:** render thread. **Saving:** -8..-10 RT % (2.0-2.5 ms a frame).

**Today.** Every texture is checked once per frame it is bound (`render/textures.cpp:1151`,
`t.checked_frame != g_frame`): linear textures through `RefreshLinearLocked` (`:773`), which hashes every guest byte
(`ContentHash`, `:276`) every frame for a plain linear texture, every frame for 8 frames after each unlock of a
lock-driven one (the font cache, 1920x1920, is lock-driven) and every 30 frames otherwise; tiled ones through
`SampleHash` (`:247`), which gathers 2 x (512 + 8 x 64) bytes spread through the texture (one cache miss per span)
and hashes them. A "dynamic" linear texture (changed at two checks in a row) is uploaded again every frame
(`:804`, `Upload(t, fetch, true)`), its whole content read at the next kick (`FlushTextureFillsLocked`).

```cpp
// textures.cpp:806 (plain linear texture, every frame it is bound)
const uint64_t hash = ContentHash(fetch, t);           // PmullHash over the whole texture
// textures.cpp:1156 (tiled, every frame)
const uint64_t hash = SampleHash(fetch, t);            // 18 scattered lines gathered, then hashed
```

**Instead.** The same proof package 34 gives the vertex copies (`CardsProve`, `render/draw.cpp:793`), over the
texture's guest extent (`t.sample_pages` / `t.sample_sizes`, already known per texture by `KnowGuestExtent`). Keep with
each texture the hash last computed and the interval (`g_flush_serial`) it was computed in; a check reuses it while no
page of the extent was found written in or after that interval.

```cpp
// HostTexture: uint64_t last_hash = 0, last_hash_serial = UINT64_MAX, read_serial = UINT64_MAX;
bool TextureCardsProve(HostTexture &t, uint64_t proven) {     // draw.cpp's PagesClean, exported
  if (!banjo::renut_gpu::StoreCardsOn() || proven == UINT64_MAX) return false;
  bool clean = true;
  for (u32 i = 0; i < 2; ++i)                                  // both ranges are read: their cards are cleared
    if (t.sample_pages[i] && t.sample_sizes[i])
      clean &= PagesClean(GuestPhysicalPtr<const u8>(t.sample_pages[i] << 12), t.sample_sizes[i], proven);
  return clean;
}
uint64_t CheckedHash(const TextureFetch &fetch, HostTexture &t, bool sampled) {
  KnowGuestExtent(fetch, t);
  if (TextureCardsProve(t, t.last_hash_serial)) {
    t.last_hash_serial = FlushSerial();                        // proven again in this interval
    return t.last_hash;                                        // the bytes are those last hashed
  }
  t.last_hash = sampled ? SampleHash(fetch, t) : ContentHash(fetch, t);   // today's
  t.last_hash_serial = FlushSerial();
  return t.last_hash;
}
// RefreshLinearLocked / the tiled branch: every ContentHash(...) / SampleHash(...) becomes CheckedHash(...).
// The dynamic branch: skip Upload when TextureCardsProve(t, t.read_serial); FlushTextureFillsLocked sets
// t.read_serial = g_flush_serial when it reads the texture (it runs after FlushVertexFillsLocked's increment).
```

A second step for the lock-driven font cache: an unlock re-uploads the whole 1920x1920 texture; with the cards, only
the rows of the pages found written need reading and converting (rows of a linear texture map to pages by its pitch).

**Why identical.** Each decision (upload or not, `changes`, `dynamic`) compares a hash with `t.content_hash`. When no
page of the extent was written since the interval the last hash was taken in, the bytes are the ones hashed then, so
today's code would compute the same hash and take the same decision; when a page was written, today's hash runs. A
dynamic texture's re-upload is skipped only when no page was written since the bytes were read at the last upload,
so the GPU's copy already holds them. The cards cover every writer of guest memory the vertex copies rely on (generated
stores, the CRT hooks, file reads, the hand natives, the memexport write-back); the race of a card read before its
store's data is visible is handled as for the streams (a page found written in interval S counts as written for every
proof of S or earlier). **Check:** extend `banjo_renut_store_cards_verify` to textures (hash anyway; count and log
"texture changed under clean cards" by address, which must stay 0 on the scripted route); count bytes not hashed in the
"store cards" line.

**Evidence.** `PmullHash` 8.03 RT %, 93 % of it called from `ContentHash` (7.4 RT %, 1.9 ms a frame: about 5-6 MB of
linear texture hashed a frame at the A57's ~3 GB/s); `SampleHash`'s gather is the 2.46 RT % of `memcpy` called from
`BindTextureLocked`, its hash 0.52 RT %; texture uploads (`ReadLevel` + `memcpy` + `DecodeBlockRow`) ~1.6 RT %.
`BindTextureLocked` is 18.6 RT % inclusive (4.7 ms a frame).

**Estimate.** Hashing drops to the textures whose pages were written: -7..-8 RT % for the linear checks, -2.5 for the
tiled ones, part of the uploads; the card reads cost 4 byte loads per page (a 7 MB texture is 1,800 pages, ~10 us;
the typical tiled texture 16-64 pages): -8..-10 RT %. **Effort/risk:** medium / low (a missed writer is the same risk
the stream copies already carry, caught by the verify mode).

### 2. Float constants as pipelined constant-buffer updates of the changed groups

**Kind:** render thread + plume (deko3d only; Vulkan keeps today's path). **Saving:** -4.5..-6 RT % (1.1-1.5 ms).

**Today** (`render/draw.cpp:1313`). Per stage and draw, the reuse check compares the registers the shader reads with
the last upload; on any difference a new 4,112-byte allocation (4,352 with the ring's 256-byte alignment) is taken,
every used group is byte-swapped into it (write-allocate misses on ring lines two frames cold), register 256 zeroed,
the whole allocation cleaned at the submit (`Publish`), and the UBO rebound (`BindConstantSet` →
`setGraphicsDescriptorSetDynamic` → `bindSetBuffers` → `dkCmdBufBindUniformBuffer` at the draw).

```cpp
UploadAllocation alloc = Video::UploadsLocked().Allocate(kFloatConstantsUboBytes, 256);   // draw.cpp:1356
std::memset(out + 256 * 16, 0, kFloatConstantsUboBytes - 256 * 16);
for (uint64_t remaining = groups; remaining;) { ... lanes byteswapped ... memcpy(out + group * 64, lanes, 64); }
```

**Instead.** One UBO per stage (4,352 bytes, zeroed at creation, so register 256 stays 0), bound once per command list
(`graphicsUbo[0]` / `[1]`). Per draw, the groups the shader reads whose bytes differ from what this list last sent are
byte-swapped and sent with `dkCmdBufPushConstants` (deko3d's pipelined constant-buffer update: `ConstbufSelectorSize` +
`LoadConstbufData`; draws recorded before it keep the old values, the rule plume already relies on for push UBO slot 5,
`plume_deko3d_commands.cpp` `setGraphicsPushConstants`). Runs of adjacent groups go in one call.

```cpp
struct SentConstants {             // per stage, reset at each list's begin (s.list_generation)
  uint64_t list = ~0ull;
  u32 valid[8];                    // registers the GPU's UBO holds with the device's bytes (literal ones excluded)
  const ShaderCacheEntry *literals; // whose literal registers it holds
  u8 guest[256 * 16];              // those bytes, as the guest has them
};
uint64_t push = 0;
for (each group g the shader reads)                       // used_groups precomputed (proposal 12)
  if (registers of g read by the shader are not all valid, or differ from sent.guest) push |= bit(g);
if (entry has literals && sent.literals != entry) push |= literal groups (+ the previous shader's, made invalid);
ForEachGroupRun(push, [&](u32 first, u32 count) {
  swap count * 64 bytes from constants.Register(first * 4) into a stack buffer (+ literal overlays);
  memcpy into sent.guest; mark valid; plume::Deko3DPushUniform(cmd, ubo_addr, 4352, first * 64, buf, count * 64);
});
```

**Why identical.** For every draw the GPU reads, in each register the shader reads, the device's value byte-swapped (or
the shader's literal), exactly what today's upload holds; registers the shader does not read are not read either way
(today they hold stale ring bytes). Ordering: a constant-buffer update is executed in command order with the draws.
**Check:** a PC/Ryujinx compare mode that, per draw, reads back nothing but asserts the CPU model (`sent.guest` and
literal ownership) equals today's upload bytes for the used registers; RenderDoc on Ryujinx shows the UBO contents per
draw; frame-matched video against today's build.

**Evidence.** `UploadFloatConstants` 6.50 RT % (its hottest instructions: the guest loads 47 samples, the ring stores
~25, the `UsedGroups` loop ~45, the `valid` rebuild); by estimate (1,000-1,500 uploads a frame of 4,352 bytes) more than half
of the 8-11 MB cleaned a frame are float-constant allocations (2.8 RT % of cleaning in all); UBO rebinds `bindSetBuffers` 1.01 + `setGraphicsDescriptorSetDynamic` 0.37 +
`bindGraphicsBuffers` 1.01 + `BindConstantSet` 0.26 RT % (partly shared with SharedConstants).

**Estimate.** The guest loads and compares stay; the stores shrink to the changed groups (a world matrix and a few
material registers: 1-4 groups instead of 10-40), no allocation, no clean, no rebind: -4.5..-6 RT %. GPU: about
200-400 KB of constant data a frame through the command processor; measure with the GPU profile (the risk of this
proposal). **Origin:** the Xbox 360's own D3D (constants written into the command buffer); Unleashed's "constants in
host byte order, copy on upload" goes half way. **Effort/risk:** medium / medium (GPU-side cost; the literal registers'
ownership must be exact; Ryujinx emulates `LoadConstbufData`, so the emulator checks pictures).

### 3. Texture-binding memo keyed by texture, and a narrow `g_texture_epoch`

**Kind:** render thread, bug fix. **Saving:** -4..-5.5 RT % (1.0-1.4 ms).

**Today** (`render/draw.cpp:1429`). Package 34's memo has one entry per fetch slot, valid while `g_texture_epoch` holds;
a miss runs `BindTextureLocked` and `SamplerSlotLocked`. **Bug:** the epoch is bumped by every texture upload
(`textures.cpp:354`, including every dynamic texture's per-frame upload), by every resolve even into an existing
destination with the same description (`:686`, `ResolveTarget`, ~20 a frame), and by every words copy (`:669`). With
25-40 bumps a frame and a slot whose texture changes with the material, the memo almost never hits: `SamplerSlotLocked`
(called only on a miss, `draw.cpp:1478`) is 1.42 RT % of self time on its own, `BindTextureLocked` 4.14.

**Instead.** (a) A direct-mapped memo of 512 entries keyed by the texture header VA (+ the device's 6 fetch words and
the header's 6 words, compared raw, before `FromGuest` decodes them), valid for the frame. (b) Bump the epoch only for
what changes a binding: a texture created or retired, a new view or slot, an entry of `g_resolve_targets` or
`g_depth_words` that changes value (`ResolveTarget`: bump only when it made the texture or the map entry differs), and
depth resolves for entries whose `fx` named a depth source (a second, "depth" epoch, so colour resolves do not touch
the plain textures). Uploads keep `layout` changes, which the hit path redoes (`TransitionTextureLocked`).

```cpp
struct TextureMemo { u32 va; be_u32 device[6], header[6]; uint64_t frame, epoch, depth_epoch; u32 dim, index, sampler; BindEffects fx; };
TextureMemo g_texture_memo[512];
TextureMemo &m = g_texture_memo[(texture_va * 0x9E3779B1u) >> 23];
if (m.va == texture_va && m.frame == g_frame && m.epoch == g_texture_epoch &&
    (!m.fx.depth || m.depth_epoch == g_depth_epoch) && same 12 words) { transitions, counters; continue; }
```

**Why identical.** The miss path is a function of the address, the two sets of words and the state the epochs cover;
within a frame its other effects are idempotent (`used_frame`, the per-frame content check already done at the first
bind) or redone on a hit (transitions, counters). Uploads change a texture's content, not its slot, dimension, scale or
sampler. **Check:** the PC compare mode (run both, compare slot, dimension, sampler and effects per bind); hit and miss
counters in the `[renut]` line.

**Evidence.** `BindTextureLocked` 4.14, `SamplerSlotLocked` 1.42, `FromHeader` 0.84, `TextureSlot` 0.43, `FromGuest`
0.30, `ResolveTraceOn` 0.22 RT %, and about 100 samples (2.2 RT %) of `DrawStateLocked` in the `FillTextures` loop.

**Estimate.** The miss path then runs about once per texture per frame (200-400) instead of per bind (thousands):
-4..-5.5 RT %. **Effort/risk:** small-medium / low.

### 4. Memexport bookkeeping in one pass, with per-shader and per-layout summaries

**Kind:** render thread, bug fix. **Saving:** -4..-5 RT % (1.0-1.25 ms).

Package 34 did not take its proposal 4 ("the rest of the walk is a bitmap test per stream"). The profile says the walk
is ~11 RT %: exports are pending all frame (particles), so every draw runs it, and its cost is not the granule test but
the walks around it.

**Today.**
- `GatherMemExportStreams` (`draw.cpp:2479`) runs four times per draw: for VS and PS in `DrawNeedsExportFlush`'s
  capacity check (`:2811`) and again in `DrawStateLocked` (`:3494`), each reading the 32-byte `memExportConstants` of a
  cold `ShaderCacheEntry` (its hottest instruction, the first load, has 35 of its 79 samples).
- **Bug:** `ConvertExportedStreamsLocked` (`:3100`) declares `Job jobs[16]` whose `ExportedStream` has default member
  initialisers (`elements[16] = {}`): ~1.8 KB of stack is zeroed on every draw (about 20 sampled `str q31` / `stp xzr`),
  then it walks all 16 `layout.streams[].used` bytes (32-byte stride: 37 samples on that load) and calls
  `PendingExportSlots` + `FindExportedStream` again for streams `DrawNeedsExportFlush` has just examined.
- `TextureGuestExtent` (`textures.cpp:1178`) keeps a `thread_local Entry memo[1024]`: ~44 KB, larger than the A57's
  32 KB L1D, and with `-mtp=soft` (the build's flags) every access is a call to `__aarch64_read_tp`; it runs for every
  texture of every draw.
- `MemExportBufferLocked(s)` (`:3501`), `getDeviceAddress()` (virtual) and `Deko3DBindMemExport` per draw.

**Instead.**
1. `ShaderRecord::exports` (any `memExportConstants` bit, set when the record is made): `GatherMemExportStreams` is not
   called for the 99 % of shaders without exports; when it is, once per draw, its result passed from
   `DrawNeedsExportFlush` to `DrawStateLocked`.
2. `HostVertexLayout::used_mask` (bit per used stream, set by `BuildHostVertexLayout`): `ConvertExportedStreamsLocked`,
   `BindStreams` and `DrawNeedsExportFlush` loop over its bits; `DrawNeedsExportFlush` returns the per-stream
   `ExportedStream` it found (a 16-bit mask plus the found entries) for `ConvertExportedStreamsLocked` to use;
   `Job` without default initialisers (`Job jobs[16];` filled only for the jobs made).
3. `TextureGuestExtent`: a plain `static` 256-entry table (render thread only: both callers are; assert
   `t_render_thread` in PC builds), 10 KB.
4. The memexport buffer's address and bind once per list (`s.list_generation`), not per draw.

**Why identical.** Pure functions computed once instead of up to four times, the same loops over the streams that are
used (the others `continue` today), and the same memo semantics without thread-local storage. **Check:** a PC build that
also runs the old walk and asserts the same `ExportRead`, `slots` and stream jobs per draw.

**Evidence.** `DrawNeedsExportFlush` 3.88, `ConvertExportedStreamsLocked` 2.46, `GatherMemExportStreams` 1.70,
`TextureGuestExtent` 1.42 (+ `__aarch64_read_tp` lr 1.31 incl.), `PendingExportSlots` 0.65, `MemExportBufferLocked` 0.41
RT %. Part of `DrawNeedsExportFlush`'s samples are first touches of records the draw reads later anyway (the
declaration, the shader entries): those misses move rather than vanish, hence the conservative estimate.

**Estimate.** -4..-5 RT %. **Effort/risk:** medium / low.

### 5. SharedConstants built in place, and pushed by changed lines on deko3d

**Kind:** render thread + plume. **Saving:** -2..-4 RT % (0.5-1.0 ms).

**Today** (`draw.cpp:3394-3522`). Every draw clears a 1,344-byte `SharedConstants` on the stack, fills it (the 32-slot
reset loop alone writes 512 bytes), compares it with the last upload (`memcmp` of 1,344 bytes), and on a difference
(65-86 % of draws: `tessellation[3]` holds the draw's first vertex, so consecutive meshes differ) copies it into the
ring (`UploadRing::Upload`, its only caller: 2.0 RT % of `memcpy`, ~2.2 GB/s because each ring line is read for
ownership) and into `g_bound.shared_bytes` (another 1,344 bytes), then the set-7 rebind.

**Instead.** (a) CPU only, both back ends: two `SharedConstants` buffers in `g_bound` (the bound one and the next);
fields are written into the next one by small setters that compare with the bound copy and set a bit per 64-byte line
that changed; sections a draw writes only sometimes (the fetch slots, bound fetches, memexport streams, point state,
tessellation) keep a mask of what the previous draw wrote there and are zeroed only where they were written before and
not now. No `memset`, no 1,344-byte `memcmp`, no shadow copy; an unchanged draw reuses the upload, a changed one uploads
the next buffer and the two swap roles. (b) deko3d: one UBO for the shared block, the changed lines sent with
`dkCmdBufPushConstants` (as proposal 2), no ring allocation, clean or rebind; plume's `bindTextures` reads the texture and
sampler words from the CPU copy the renderer hands it (`Deko3DSetSharedWords(cmd, ptr)`) instead of the ring.

**Why identical.** The GPU reads the same 1,344 bytes per draw; line tracking is an exact compare done field by field.
**Check:** a PC mode that also builds today's block and `memcmp`s it with the in-place one at every draw.

**Evidence.** `memset` 0.73, `memcmp` (from `DrawStateLocked`) 1.18, `memcpy` from `DrawStateLocked` 0.59, `Upload`'s
`memcpy` 1.98 RT %, plus the share of the clean and of set-7 rebinds.

**Estimate.** (a) -2..-2.5 RT %; (b) -1..-1.5 more. **Effort/risk:** medium / low-medium (every writer of the block
must go through the setters; the compare mode catches one that does not).

### 6. Pipeline memo sized to the frame's pipelines, and `OutputTrimMask` memoised

**Kind:** render thread. **Saving:** -2..-2.5 RT % (0.5-0.6 ms).

**Today** (`render/pipeline.cpp:77-95`, `:918`). A 64-entry direct-mapped memo behind the last-key check. The console
binds 450-1,050 pipelines a frame (the pipeline changes at ~70 % of draws) out of a few hundred distinct keys, so the
memo thrashes and the slow path runs: XXH3 over the 192-byte key, `g_pipelines[hash]`, a key `memcmp`.
`OutputTrimMask` (`:238`) builds a `BlendDesc` per target for every draw in `MakeKey` (`draw.cpp:2431`).

**Instead.** 512 sets of 2 ways, indexed by the same cheap mix (`PipelineMemoIndex` widened to 9 bits), each way
{key, pipeline, epoch, kind}, least recent replaced; and a one-entry memo of `OutputTrimMask` (and `BlendSkipMode`) keyed by the fields they
read (color_count, color formats, blend_control[4], color_mask, alpha_to_coverage, pixel shader null or not).

**Why identical.** Same results for the same keys (the memo already returns what the slow path returned, with its
epoch rule); the masks are pure functions of the fields compared. **Check:** memo hit counters; a PC mode comparing memo
and slow-path pointers.

**Evidence.** `GetPipelineSlow` 1.01, `XXH3_len_129to240_64b` 0.54, `memcmp` from the two pipeline functions 1.13,
`OutputTrimMask` 0.45, `BlendDesc` ~0.3 RT %; `GetPipelineLocked` 3.9 RT % inclusive.

**Estimate.** -2..-2.5 RT % (the memo grows to ~220 KB, which the 2 MB L2 holds; a hit touches the 4 lines of one way).
**Effort/risk:** small / low.

### 7. GPU-profile labels only in profiled frames, memoised per shader record

**Kind:** diagnostics (on in the shipped renut.toml: `renut_gpu_profile = 10.0`, `banjo_renut_diag_gpu_shaders =
true`), bug fix. **Saving:** -1.8..-2.2 RT % (0.45-0.55 ms) while the diagnostic is on.

**Today** (`draw.cpp:3616-3623`). Every draw computes `ShaderProfileLabel` (`:1053`: `TargetsProfileLabel`, a 64-bit
key, `std::unordered_map::find`) and calls `CountDraw`; since 17:09 `gpu_profile.cpp` times only 1 frame in 4 with the
breakdown, and `CountDraw` / `Mark` return at once in the other frames, so three label lookups in four are thrown away.

**Instead.** `gpu_profile::Active(s)` (the slot's `active`) checked first: no label at all in untimed frames; in timed
ones a two-entry memo in the pixel shader's `ShaderRecord` (target label, blended → label) before the map.

**Why identical.** Diagnostics only; the timed frames' labels and counts are the same. **Evidence.**
`ShaderProfileLabel` 2.01 RT % self, `TargetsProfileLabel` called from it, the map's node walk. **Effort/risk:** small /
very low. (Turning the diagnostic off in renut.toml removes it entirely; this keeps it on for the user at ~0.1 ms.)

### 8. Targets path: framebuffer by last key, surface memo hashed, viewport memo

**Kind:** render thread, bug fix. **Saving:** -1.5..-2 RT % (0.4-0.5 ms).

**Today.**
- **Bug:** `GetFramebufferLocked` (`render/surfaces.cpp:454`) searches `g_framebuffers`, every framebuffer made, linearly
  with an 80-byte key compare, on every draw (`BindTargets`, `draw.cpp:1078`): 0.88 RT % of self time.
- `GetHostSurfaceLocked`'s memo (`surfaces.cpp:107-112`) has 16 entries indexed by `(surface_va >> 4) & 15`; header VAs
  that share bits 4-7 (allocation alignment) collide, and the global epoch is bumped by every surface release. Its slow
  path still runs about once per draw: `GetHostSurfaceSlow` 0.97 RT % + `SurfaceRenderScale` + two hash maps.
- `ComputeViewport` (`draw.cpp:1141`, 0.65 RT %) per draw, mostly the same viewport as the draw before.

**Instead.** A last-key check (and a 4-entry most-recent list) before the framebuffer search; the surface memo indexed by
`(va * 0x9E3779B1) >> 26` (64 entries) and invalidated by VA on release instead of globally; a one-entry viewport memo
keyed by the device words `ComputeViewport` reads (VteControl, the six viewport floats, ModeControl, WindowOffset, the
two scissor words, the 0x29C0 word, the predication mask and tile rect) and the targets' size and scale.

**Why identical.** Same keys, same results. **Check:** hit counters; PC compare mode. **Effort/risk:** small / low.

### 9. Stream and index memo lines with the key and the bind data; a page memo for the cards

**Kind:** render thread. **Saving:** -1.5..-2 RT % (0.4-0.5 ms).

**Today.** `BindStreams` (`draw.cpp:2064`) and `CachedIndexDraw` (`:1601`) go through `EntryMemo`: a 16-byte slot
(8,192 of them for streams, 128 KB, so an L1 miss), then the map node's key (a second miss), then the node's
`proven_flush` and copy (a third). The sampled loads: 34 + 31 + 16 in `BindStreams`, 30 + 27 + 19 in `PrepareGeometry`.
`CardsProve` (1.70 RT %) reads four card bytes per page for each of ~1,000 proofs a frame, though copies share pages.

**Instead.** Memo entries of one cache line: the full key, the entry pointer, and a copy of what a draw needs when the
copy was proven in this interval (`proven_flush`, buffer, offset, bytes; for indices `lo`, `max_index`, count, width),
written back whenever the node changes (drop, proof, new copy). A hit with `proven_flush == g_flush_serial` binds from
the line alone. And `g_page_checked[page] == u32(g_flush_serial)` (a 512 KB array beside `g_page_serial`) lets a second
proof of a page in the same interval skip its four card loads.

**Why identical.** The line is a cache of the node's fields, kept in step with every write to them; the page memo
records that the cards were already read and cleared in this interval, after which a re-read could only see writes the
next interval's proof finds anyway (the rule package 34 documents). **Check:** PC compare mode (line and node agree at
every hit). **Effort/risk:** small-medium / low.

### 10. Upload ring: clean only what is written

**Kind:** render thread, bug fix. **Saving:** -1..-1.5 RT % (0.25-0.4 ms); -0.3..-0.6 after proposal 2.

**Today.** `Publish` cleans `[published, written)` of each chunk: 8-11 MB a frame, 0.7-0.9 ms, including lines nobody
wrote. **Bug:** `UploadVertexFetchData` (`draw.cpp:2214-2266`) sizes its allocation for every copy (`total += ...`,
`:2236`) before finding that some were already queued in this chunk (`FetchCopy`), so it reserves, and the submit cleans,
space it never writes; the float-constant allocations are 4,352 bytes of which the shader's groups are typically 5-15 %;
256-byte alignment padding is cleaned too.

**Instead.** (a) `UploadVertexFetchData`: look up `FetchCopy` first, allocate only the copies to make; if the allocation
lands in another chunk than the known copies, allocate again for all (rare, at a chunk boundary). (b) Without proposal
2: allocate float constants for `[0, (last group read or literal register + 1) * 64)` (the whole 4,112 bytes when every
register is used, i.e. relative addressing, which also reads the zero register 256), binding size unchanged, and keep
4,112 bytes of room before a chunk's end (Ryujinx may copy the whole bound range). (c) `Chunk` keeps a small list of written ranges merged on append; `Publish` cleans those.

**Why identical.** The GPU reads only what was written: the fetch slots point at written copies; a shader reads only the
registers in `floatConstants` (all of them when it indexes, `shader_recompiler.cpp:878`) plus its literals, which the
upload writes. **Check:** a PC/Ryujinx poison mode that fills the unwritten bytes of each float allocation with NaNs
(pictures must not change). **Evidence.** `armDCacheClean_L0` 2.83 RT %, libnx: 8-11 MB a frame. **Effort/risk:** small /
low.

### 11. One-load globals

**Kind:** render thread, bug fix. **Saving:** -1..-2 RT % (0.25-0.5 ms).

**Today.** `GuestPtr` / `GuestPhysicalPtr` (`guest/guest_memory.h`) expand to `kernel_state()->memory()->membase`: a GOT
load, then three dependent loads (603 such chains in the renderer and plume; 43 samples, 0.93 RT %, sit on them). The
build is `-fPIE`, so every extern global is reached through the GOT (1,981 GOT accesses in the renderer and plume;
90 samples, 1.9 RT %, on those sequences with their first dereference). `banjo::renut_gpu::Latch()` was emitted out of line and is called 7 times per draw from `DrawStateLocked`
(`bl`, a GOT load and an `ldarb` each; 0.41 RT % self). `Video::UploadsLocked()`, `CommandListLocked()`,
`TextureFrameLocked()`, `MinAnisotropy()`, `ResolveTraceOn()` are out-of-line one-load functions called per draw or per
texture.

**Instead.** `inline u8 *g_guest_vbase, *g_guest_pbase` (set once after `Memory::Initialize`; the bases never change)
declared `__attribute__((visibility("hidden")))` with `GuestPtr` reading them; the same attribute on the renderer's hot
globals (`g_state`, `g_latched`, `g_frame`, the memos), so they are one `adrp` + `ldr`; `LatchTuning()` called before the
render thread starts and `Latch()` reduced to `return g_latched;` (no acquire load), marked `always_inline`; the trivial
accessors moved to headers.

**Why identical.** The same values from fewer loads. **Effort/risk:** small / very low.

### 12. Per-shader summaries in `ShaderRecord`

**Kind:** render thread. **Saving:** -1..-1.5 RT % (0.25-0.4 ms).

**Today.** `UploadFloatConstants` recomputes `UsedGroups(used)` (a loop over 8 words with `ctz` and masks, ~45 sampled
instructions) and rebuilds `last.valid` from the groups for every upload; `DrawStateLocked`, `DrawNeedsExportFlush`,
`DescribeBoundFetches` and `MakeKey` read `textureFetchMask`, `vertexFetchMemoryMask`, `vertexInputCount`,
`memExportConstants`, `floatConstants`, `literalCount`, `shaderFlags` from `ShaderCacheEntry` (120 bytes in rodata, 2-3
cold lines per shader per draw: e.g. 21 samples on `ldr x0, [x24, #48]`, 17 on `vertexInputCount`, 18 on
`vertexFetchMemoryMask`).

**Instead.** When a `ShaderRecord` gets its entry (`CreateShaderRecord`), copy into it one 64-byte line: `used_groups`
(64 bits), the group-valid masks (`u32[8]`), `exports` (proposal 4), `texture_mask`, `fetch_mask`, `has_bound_fetch`,
`literals`, `pixel_kill_depth`. The draw reads one line it has already touched.

**Why identical.** Copies of constants made once. **Effort/risk:** small / very low.

### 13. plume deko3d per draw: direct UBO binds, plain statistics

**Kind:** plume, bug fix. **Saving:** -1..-1.5 RT % (0.25-0.4 ms; less after proposals 2 and 5).

**Today.** `BindConstantSet` → `setGraphicsDescriptorSetDynamic` → `bindSetTables` (two `dkMemBlockGetGpuAddr` calls)
and `bindSetBuffers` (`plume_deko3d_commands.cpp`), which walks the layout's ranges and the set's buffer vector to find
that set 5/6/7 is one dynamic UBO at slot 0/1/2. **Bug:** `drawIndexedInstanced` / `drawInstanced` still do
`GetDeko3DStats().draws.fetch_add(1)` and `bindGraphicsBuffers` `windowDraws.fetch_add(1)`, an `ldxr/stxr` loop each per
draw (`-mno-outline-atomics`, no LSE) through an out-of-line `GetDeko3DStats()`; package 34 changed `setPipeline`'s
counters only.

**Instead.** `plume::Deko3DBindUniform(cmd, slot, addr, size)` (as `Deko3DBindFetchData`) for the three constant sets on
deko3d; the per-draw counters as plain members of the command list, added to `g_stats` at `end()`.

**Why identical.** The same `graphicsUbo` extents and the same counts. **Evidence.** `bindSetBuffers` 1.01,
`setGraphicsDescriptorSetDynamic` 0.37, `drawIndexedInstanced` 1.25 RT % self. **Effort/risk:** small / low.

### 14. Persistent fetch-data copies proven by the store cards

**Kind:** render thread. **Saving:** -1..-2 RT % (measure the share first).

**Today.** Every draw whose vertex shader reads memory (150-740 a frame) copies each fetch buffer into the ring
(`UploadVertexFetchData`), read at the kick (`FlushVertexFillsLocked`, `draw.cpp:3233`: `memcpy` 1.98 RT %), cleaned at
the submit; a buffer used by several draws of one interval is copied once.

**Instead.** Fetch buffers whose bytes held for a few intervals get a persistent copy in the copy pool, proven per
interval at first use by `CardsProve`, exactly as the vertex streams are; the draw's fetch window then points into the
pool chunk (`Deko3DBindFetchData(chunk base, chunk size)`, slots as offsets in it). Dynamic buffers (bone palettes
rewritten every frame) stay on today's path after `kMaxCacheDrops`.

**Why identical.** The same per-interval proof rule as `banjo_renut_exact_streams` (a copy is current when no page of it
was written since the interval it was proven in). **First:** count in the `[renut]` line the fetch bytes copied whose
pages were clean (if most are bone palettes, the gain is small). **Effort/risk:** medium / low-medium (all of a draw's
copies must be in one pool chunk, or the draw keeps today's path).

### 15. Immediate draws as deltas against a running device

**Kind:** render thread + guest side. **Saving:** -1..-2 RT %, guest -2..-3 % of a core.

Package 34's proposal 3(c), not taken; the render-thread side adds to it. Immediate draws (17.6 RT % inclusive) each
read a fresh 12.7 KB capture written by a game thread on another core (`backend/queued.cpp:121`), so the scattered
fields `DrawStateLocked` reads are cross-core misses; command-buffer draws read `g_replay_state[depth]`, one buffer that
stays in core 1's caches. With the delta form (changed 64-byte chunks + pending constant groups, applied by the render
thread to a running device per device, as `Replay` does) both kinds read a local device. Same bytes; check by
rebuilding the full snapshot in a PC build and comparing. **Effort/risk:** medium / low-medium.

### 16. Prefetch the draw's records at entry

**Kind:** render thread. **Saving:** -1..-3 RT % (speculative).

Most of the render thread's sampled instructions are first touches of records (shader records and entries, the
declaration, stream and index memo slots and nodes, texture headers in guest memory, pipeline objects), serialised by the
code between them. At `DrawStateLocked` entry, once the VS/PS/declaration VAs and the stream fetch words are known,
`__builtin_prefetch` the records, the 1-3 stream memo slots, the texture headers of the fetch mask and the surface
descriptors. No effect on results. Measure with the console profile (samples on those loads before and after).
**Effort/risk:** small / very low.

### 17. DC ZVA before whole-line writes into the upload ring

**Kind:** render thread (if proposals 2 and 5 are not taken). **Saving:** -1..-1.5 RT %.

Ring lines are two frames cold, so each store misses and the line is read from memory before it is overwritten: the
SharedConstants `memcpy` runs at ~2.2 GB/s, and the float-constant stores are among `UploadFloatConstants`' hottest
instructions. Every float group is one aligned 64-byte line and SharedConstants is 21 whole lines at a 256-byte
alignment: `dc zva` each line, then write it (the project already uses DC ZVA on the Switch, package 24). Same bytes
(the line is fully overwritten). **Effort/risk:** small / low (check `DCZID_EL0` block size once at start).

### 18. Upload-ring chunk churn and `makeBlock` logging on the render thread

**Kind:** render + plume, bug fix. **Saving:** -0.3..-0.5 RT % on average, and the hitches.

**Bug.** `banjo_renut_upload_trim_frames` (120) releases a slot's extra 32 MB chunk after 60 frames without a heavy frame;
the next heavy frame makes it again on the render thread in the middle of a draw (`BindStreams` → `Allocate` →
`AddChunk`, seen in window 8's stacks): `dkMemBlockCreate` (a 32 MB heap allocation, `nvMapCreate`,
`nvAddressSpaceMap`), and `makeBlock`'s log line, which walks the whole newlib heap (`mallinfo`:
`__malloc_update_mallinfo` 0.22 RT % self) and writes `deko3d.log` with an `fflush` to the SD card
(`fsdev_write` in the stacks). **Instead:** trim only after ~1,800 frames, or keep up to 2 chunks per slot; log from
`makeBlock` only on failure or with the free figures computed on the log thread. Memory policy only. **Effort/risk:**
small / very low.

### 19. Render loop: the state lock once per batch

**Kind:** render thread. **Saving:** -0.3..-0.5 RT %.

`render_thread.cpp:84-91, 109-113`: each queued item takes and releases `State().mutex` and reads the clock twice
(`mutexLock` 0.24, `mutexUnlock` 0.17 RT %; ~500 items a frame). Run a batch's consecutive locked items under one lock
(released every N items or when a game thread is waiting, `g_record_mutex` style), and read the clock once per item.
Same order of work. **Effort/risk:** small / low (game-thread hooks that take `State().mutex` wait at most one batch).

### 20. No heap allocation per draw

**Kind:** render thread. **Saving:** -0.3..-0.5 RT %.

`g_queued_streams` (`draw.cpp:445`) is an `unordered_map` that allocates a node for every uncached stream and frees them
all at each kick; `ExpandIndices` (`:1528`) builds a new `std::vector` per fan or quad draw; `ClearTargets` (`:3818`)
two vectors per clear and two passes over `g_targets` in `ColorAliasesLocked`. A flat open-addressed table with a
generation (as `g_fetch_copies`), and render-thread scratch vectors. `_malloc_r` 0.24, `_free_r` 0.22 RT % + the malloc
lock. **Effort/risk:** small / very low.

### 21. `DescribeBoundFetches` and shader lookups memoised per (vertex shader, declaration)

**Kind:** render thread. **Saving:** -0.3..-0.5 RT %.

`DescribeBoundFetches` (`draw.cpp:2153`) matches the shader's inputs against the declaration's elements per draw (a
nested loop over cold `g_shaderVertexInputs`); only the stride depends on the draw (`GetStreamSource`). Keep per
(vs entry, declaration) the matched (input, fetch index, offset, type) list; per draw only the strides. Same values.
**Effort/risk:** small / low.

### 22. Smaller items

**Saving:** -0.3..-0.5 RT % together. `SwapEndian(2, bools, sc.booleans, 32)` (`draw.cpp:3403`) inline as two `rev32`
vectors; `CountDrawKind`'s census (`:3529-3548`) computed from the key fields only on the frames the report counts;
`ResolveTraceOn()` (`banjo_renut_tuning.cpp:987`) an inline relaxed load; `TextureFrameLocked()` inline (it is called per
fetch slot through a `bl`); `StreamDumpLog` / `g_dump_fetch` static guards hoisted to a latched bool. Exact.

### 23. The submit's cache clean on a helper core

**Kind:** render thread (moved). **Moves:** 0.4-0.9 ms a frame off the render thread.

`DC CVAC` by address is broadcast in the inner shareable domain, so another core can clean the ring. At each kick hand
the ring ranges written since the last kick to a worker on core 2 (priority below the game's threads, as the uam
thread); at the submit, `Publish` waits for the worker's completion (and its `dsb sy`) and cleans what remains. Same
cache state at the submit. Worth it only after proposals 2, 5 and 10 if the clean is still large. **Effort/risk:**
small-medium / low.

### 24. Kick-time fills on a helper core

**Kind:** render thread (moved). **Moves:** 0.5-1 ms a frame.

`QueueKickFlush` (4.3 RT % inclusive: vertex conversions, fetch copies, texture reads at each kick) is pure CPU work
from guest memory into ring memory. A worker can do it if the kick's fence (`KickFence`) is written only after the
worker's read is complete: the render thread queues the fills, continues with draws, and the fence-completion item
waits for the worker. Same bytes read at the same point of the game's protocol. **Effort/risk:** medium-large / medium
(fence ordering).

---

## 4. Ideas looked at and rejected

| Idea | Why not |
|---|---|
| A deko3d record worker (package 28b's, for deko3d) | plume + deko3d are ~9.6 RT % on deko3d (NVK was 22 %); the proxy's per-call copy would cost a large part of what it moves, and it is a move, not a saving. |
| Pre-encoded pipeline binds (`dkCmdBufReplayCmds` of a pipeline's recorded method words) | ≤1 RT % (`setPipeline` 1.36 + `dkCmdBufBindShaders` 0.71), and it depends on deko3d's internal shader header fields and code-segment offsets. |
| Smaller bound UBO size instead of proposal 10(b) | Out-of-range constant reads return 0 on Maxwell, not the bytes a smaller allocation would leave; not exact if a register outside the size were read. 10(b) keeps the bound size. |
| Taking `first_vertex` out of SharedConstants (base vertex or instance id) so the dedupe hits | Every shader and the DKSH cache rebuilt; proposal 5(b) makes the block's size per draw small anyway. |
| Fewer card intervals (no `++g_flush_serial` at every kick) | The kick is where the game may learn the GPU is done; the per-interval proof is the exact rule. |
| NEON `ExpandElement` / index conversions | Package 34's proposal 20 (not taken); conversions are 1.6 RT % and must match `DecodeComponent` bit for bit. |
| Several submissions per frame so the GPU starts earlier | A GPU-latency item, not CPU; each costs a fence and a flush. For the GPU agenda. |
| Write-combined (uncached) upload ring | Measured slower on the A57 (2.5 vs 5.2 GB/s, `banjo_renut_upload_cached`). |
| `State().mutex` held for the whole frame | Blocks the game threads' hooks that take it (resource creation, shader destruction). Proposal 19 batches instead. |
| Faster content hash | Bandwidth bound (the hot PC is the loop's `ldp`); only hashing fewer bytes helps (proposal 1). |
| Hashing textures on another core | Same as the streams' check worker (0 % on the console, removed in 28b); proposal 1 removes the hashing instead. |
| Turning the GPU-profile diagnostic off | The user's request (package 30); proposal 7 makes it cheap. |
| LTO / PGO for the cross-file accessors | Build-wide; proposal 11 gets the hot ones with header inlines and a visibility attribute. |

## 5. For the next console profile

- Keep the ELF of the exact NRO next to the logs (this one was; symbolization was exact).
- Counters worth adding to the `[renut]` line before and after: texture-memo hits and misses (proposal 3), surface and
  pipeline memo hits (6, 8), linear-texture bytes hashed a frame and textures proven by cards (1), fetch-data bytes
  copied and bytes reserved but not written (10, 14), ring bytes by kind (float constants, shared, indices, vertices,
  fetch data, texture staging) and bytes cleaned (2, 5, 10), upload-ring chunks made (18).
- Proposal 2 needs the GPU profile with it on and off (the constant-buffer updates' GPU cost).
