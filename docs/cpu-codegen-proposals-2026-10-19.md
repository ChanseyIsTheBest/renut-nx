# CPU proposals of 2026-10-19: recompiler codegen, generator bug fixes and the render thread

Twenty-five proposals to cut CPU time on the console (Cortex-A57, 1020 MHz), ranked by the saving expected there.
Read-only research: nothing in the project was changed, built or run. Scope, as asked: what the ReXGlue generator
emits and the runtime helpers that code calls (no PGO, LTO, compiler flags or build changes); generator and pipeline
bugs that cost speed ("bug fix"); and, since the second scope note, CPU work on Renut's render thread and on the guest
side that feeds it. Every proposal keeps the game's results and pixels identical; each says why, and what guard or
check it needs.

Already done or measured, so not proposed again (docs/codegen-enhancements.md, docs/enhancements*.md, the memory
notes): arguments in registers (+2.8 %), lazy byte order (<1 %), non-volatile and non-argument locals, loop / leaf
locals and their opt-out list, FP mode lookahead and the flush-flow proofs, the 33 post-codegen steps, the localized
and hand-written natives, the check worker for stream hashes (answered 0 % on the console, removed in 28b), a per-frame
stream proof (not provable: about 20 guest functions read the fence word directly), page-protection write tracking
(Switch page faults corrupt NEON registers), MSAA / resolution changes.

## 0. What package 34 took (2026-10-19, after this report)

The user asked for every proposal judged worth it in the next console package. Each is behind a switch for an A/B
where it has a run-time form; the codegen ones have their CMake switch back (one NRO ships).

| # | Proposal | Status | Where / switch |
|---|---|---|---|
| 1 | Store cards instead of re-hashing cached streams | **taken**, with a fix of the race in the design above: a card the check reads before the store's data is visible is consumed in that interval, so a page found written in interval S counts as written for every copy proven in S or before, and the copy is hashed again in S+1 (two hashes per change, none for unchanged data). A store marks its first byte's page only; a page counts as written when the page before it was too (an unaligned store into the next page). No per-store `stlr` | `tools/store_cards.py` (post-codegen, last), `REX_CARD` in the pch, `BANJO_GEN_STORE_CARDS`; cards in guest 0-1 MB (`Memory::Initialize`), `MarkStoreCards*` (`rex/system/xmemory.h`) in the CRT memory hooks, `PhysicalHeap::TriggerCallbacks` (file reads, decommit, release), the hand natives (`banjo::hot::Card`), the memexport write-back; `PagesClean` / `CardsProve` in `render/draw.cpp`; `banjo_renut_store_cards`, PC test `banjo_renut_store_cards_verify`; interlock `g_banjo_store_cards` |
| 2 | Texture-binding memo per fetch slot | **taken** | `FillTextures` memo, `g_texture_epoch` (`render/textures.cpp`); `banjo_renut_cpu_memos` |
| 3 | `NewOp` fast path | **taken in part**: (a) the per-thread lookup cache (no lock, no map lookups while nothing changed); (b) and (c) not | `guest/command_buffers.cpp`; `banjo_renut_cpu_memos` |
| 4 | Memexport bookkeeping memo | **not taken**: `TextureGuestExtent` already has a memo, and the rest of the walk is a bitmap test per stream | |
| 5 | Latched getters, single-writer counters | **taken** | `banjo_renut_tuning.h` (`Latch()`), `Bump` |
| 6 | Pipeline memo | **taken** | `render/pipeline.cpp`; `banjo_renut_pipeline_memo` |
| 7 | Hot-edge inline copies + the toml-comment hook bug | **taken**: `direct_calls.py` drops `#` comments of the tomls and CMakeLists (24 functions were "hooked" by comments only); `inline_leaves.py --hot app/config/hot_edges.txt` (six leaf edges of the profile, callees up to 128 instructions, copied into the callers' files only) | `REX_INLINE_LEAVES_OFF` |
| 8 | Render-target lookup memo | **taken** | `render/surfaces.cpp`; `banjo_renut_cpu_memos` |
| 9 | Constants by dirty groups from a host-order shadow | **not taken** in this package (the replay path already tracks dirty groups; the rewrite's risk against ~2 %) | |
| 10 | Vector any-mode forms | **not taken** in this package: a guarded helper and a unit test per VMX float operation plus a run analysis in the generator | |
| 11 | Stream / index binding memo | **not taken**: `g_stream_memo` / `g_index_memo` already answer the lookups | |
| 12 | Locals opt-out re-measured | **taken in part**, measured: of the four hottest opted-out functions, `sub_829A6DE8` (the culling test: 249 -> 183 instructions, loop memory operations 91 -> 29) and `sub_82959230` (3,176 -> 2,980) keep locals; `sub_82BDAED0` (+19 % instructions, +1,465 stack spills and reloads with locals) and `sub_829A1D88` (406 -> 770 stack operations) spill and stay on the list. The rule for other ports: `docs/codegen-enhancements.md`, "Loop and leaf locals: choosing the functions" | `app/config/locals_optout.txt` |
| 13 | plume deko3d binding diff | **taken**: the stages kept at creation (no `dkShaderGetStage` per bind), nine state groups hashed apart and bound only when they differ; the dynamic depth bias and stencil reference make their groups unknown | `plume_deko3d*.{h,cpp}` |
| 14 | Upload ring clean-only | **taken** | `plume_deko3d.cpp` |
| 15 | Single-precision bit forwarding | **taken**, wider than proposed: with `fp_copy_forwarding` a store takes the copy form in any mode and through any instruction that does not write its register or branch, in-place fneg / fabs / fnabs followed; the load takes the copy form whenever one of its stores does (the same test, so the bits local is always written) | `fp_copy_forwarding` (generator, `renut_config.toml`) |
| 16 | Native LZX decoder | not taken (large, medium risk) | |
| 17 | Native culling test | not taken (proposal 12 first) | |
| 18 | `Replay` | **taken in part**: the draws' predication set once per replay (only draws read it) | `guest/command_buffers.cpp` |
| 19 | Critical sections inline | not taken in this package (generator + runtime, medium) | |
| 20 | NEON conversions | not taken: the 16-bit index loop's swap test is loop-invariant (unswitched and vectorised by the compiler) | |
| 21 | Kernel waits without `dynamic_cast` | **taken**: `WaitHandle::posix_wait_handle()` (one virtual call) | `sdk/include/rex/thread.h`, `threading_posix.cpp` |
| 22 | Guest-side hooks | **taken in part**: `banjo::renut_gpu::Active()` and `renut::gpu::State()` inline; the export shim inlined by 25 | `banjo_renut_select.h`, `backend/video.h` |
| 23 | Pooled clears and resolves | **taken**: from the draw-capture pool, the closure one pointer | `backend/queued.cpp` |
| 24 | Native reverb | not taken (verified native code, a few 1 KB copies per audio block) | |
| 25 | Export shim without the barrier | **taken**: `HostToGuestFunction` always inline into the hook, no compiler barrier | `sdk/include/rex/ppc/function.h` |

## 1. Where the CPU time goes (console profile 21)

Source: `C:\Users\Potatoe\AppData\Local\Temp\rex_profile (21).log`, the console run of 2026-10-19 (run-49 NRO,
emulator toml: `banjo_renut_record_worker = false`, per-shader GPU diagnostics off). Only the 19 "gameplay" windows
(game 3-33 fps): 25.1 fps, CPU total 186 % of 400 %.

**Symbolization.** The ELF of the profiled NRO was not kept. The 2026-10-19 12:33 Switch ELF
(`app/out/switch/renut`, copied to the session scratchpad) has the same game code, shifted by +0x240: 191 of 193 return
addresses in that range land right after a `bl`/`blr` with that shift (random alignment gives about 5 %). The renderer
region is shifted by 0x4D0-0x9A0 in pieces. `render/draw.cpp` and `render/textures.cpp` changed after the profiled
build, so attributions to individual functions in those two files are approximate (large functions are robust). The
top render-thread entry, `PmullHash`, was checked against the disassembly: its sampled PCs are the `ldp`/`pmull`
instructions of the 64-byte loop (`banjo_renut_tuning.cpp:1006`, ELF 0x217a858). Scripts: scratchpad `sym21.py`
(piecewise shifts, per-thread self/inclusive/leaf-callee views), `insnclass.py` (mnemonic of every sampled PC).

| Thread | Share of a core | Notes |
|---|---|---|
| Render thread (prio 0x2C, core 1) | 74.7 % | host code only (Renut + plume + deko3d); ~14 % of its stacks wait on fences in Present |
| Main XThread | 31.8 % | `sub_8294EF68` interpreter, `memmove` 6.7 % (render captures), D3D state copies |
| Drawing threads 67800EA0 / 6A400EA0 / 6B40EEC0 / 6C521F20 | 14.9 / 6.3 / 2.5 / 2.1 % | `NewOp` 13-19 % of the two big ones, culling `sub_829A6DE8` 11.6 % of the first |
| Audio threads 46BDCA80 / Audio Worker / XMA | 13.9 / 3.7 / 4.8 % | reverb native, XAudio filters (`n_82B0DCB8`), limiter copy, WMA decode |
| Script thread 6B40EF80 | 10.1 % | interpreters `sub_82948928`, `sub_82BDAED0`, `sub_82949790` |
| FMV threads | 6.6 + 3.1 % | WMV decoder (intro movies only) |

Guest threads together ≈ 100 % of a core. Their samples: recompiled `sub_*` 42.9 %, recompiled XDK/CRT `rex_*`
19.0 %, localized/hand natives 8.6 %, renderer recording on the guest threads 6.4 %, SDK runtime 4.5 %, libc 4.0 %,
native reverb 3.4 %, libnx locks 1.7 %, hook thunks 0.7 %.

Inside recompiled code (4,355 sampled PCs): loads ≈ 50 % (guest `[x, w, uxtw]` 24.7 %, `[x, #imm]` 23.4 %, stack
3.2 %), stores ≈ 21.6 %, prologue/epilogue and spills ≈ 9.5 %, instructions next to `msr fpcr` 3.4 % (147 samples; the
log measures 43.6 ns for a toggling write), the any-mode float conversions' range-check branches 2.4 %. Indirect-call
dispatch: 12 samples (0.3 %).

Render thread self time (5,173 samples): `PmullHash` 25.5 % (called from `ExactHash`, the exact stream / index
checks), `memmove` 7.4, `BindTextureLocked` 5.2, `DrawStateLocked` 4.2, `UploadFloatConstants` 3.9,
`DrawNeedsExportFlush` 2.6, `armDCacheFlush_L0` 2.4, `BindStreams` 2.3, `ConvertExportedStreamsLocked` 2.3,
`Replay` 1.7, `PrepareGeometry` 1.7, `ActiveBackend` 1.6, `GatherMemExportStreams` 1.6, `GetHostSurfaceLocked` 1.6,
`GetPipelineLocked` 1.5, `memcmp` 1.5, plume `setPipeline` 1.5, `SamplerSlotLocked` 1.4, `TextureGuestExtent` 1.2,
`TextureFetch::FromHeader` 1.0, `CountSharedConstants` 0.9, `FindShaderLocked` 0.85.

Units used below: **RT %** = share of the render thread's samples (1 RT % ≈ 0.75 % of a core ≈ 0.3 ms of render-thread
CPU per frame at 25 fps); **G %** = share of all guest-thread samples (1 G % ≈ 1 % of a core). In light scenes the
render thread sets the frame time (~33 ms), so RT savings turn into frames there; in heavy scenes the GPU does.

## 2. The 25, ranked

| # | Proposal | Kind | Expected saving on the console | Effort | Risk |
|---|---|---|---|---|---|
| 1 | Dirty pages marked by the generated stores, instead of re-hashing every cached stream after each kick | codegen + render | RT -20..-23 % (≈ 6 ms/frame); guest +1.5..2.5 % of a core; net ≈ -13..-15 % of a core | large | medium (missed writer = stale geometry; verify mode catches it) |
| 2 | Texture-binding memo per fetch slot | render | RT -5..-7 % | medium | low |
| 3 | Recording fast path: `NewOp` without the global lock and map lookups, delta snapshots for immediate draws | render (guest side) | G -3.5..-4.5 % (drawing threads -15 %, main -4 %) | medium | low-medium |
| 4 | Memexport bookkeeping memoised per draw | render | RT -4..-5 % | medium | low |
| 5 | Tuning getters and statistics counters latched (no out-of-line getter or atomic per draw) | render, bug fix | RT -3..-4 %, G -0.4 % | small | very low |
| 6 | Pipeline lookup: memo before hashing, and resolved fallbacks for pending / failed specialisations | render, bug fix | RT -3..-3.5 % | small | low |
| 7 | Hot-edge inline copies (inline_leaves for mid-size callees on profiled edges) + the toml-comment "hook" bug | codegen, bug fix | G -1.5..-2.5 % | medium | low |
| 8 | Render-target, shader and declaration lookups memoised per draw | render | RT -2..-2.5 % | small | low |
| 9 | Float / shared constants by dirty groups from a host-order shadow | render | RT -2..-2.5 % | medium | low |
| 10 | Vector any-mode forms: short VMX runs in FPU mode behind a denormal guard (no `msr fpcr` pair) | codegen | G -1.2..-1.8 % | medium | low (exact by guard) |
| 11 | Vertex-stream and index binding memo | render | RT -1.5..-2 % | small | low |
| 12 | Locals opt-out list re-measured with by-value vectors and console weights | codegen, bug fix | G -0.8..-1.5 % | small | very low |
| 13 | plume deko3d binding: sub-state diffing, cached shader stages, no per-bind statistics atomics | render | RT -1.5..-2 % | small | low |
| 14 | Upload ring: no clean+invalidate per unmap (write-combined constants ring, clean-only for the rest) | render | RT -1..-2 % | small | low-medium (perf only) |
| 15 | Single-precision bit forwarding through `fmr` / `fneg` / `fabs` / `fnabs`, in any mode | codegen | G -0.6..-1 % | small | low |
| 16 | Hand-written native LZX (XCOMPRESS) block decoder | native | G -0.8..-1.2 % while streaming | large | medium |
| 17 | Hand-written native of the culling test `sub_829A6DE8` | native | G -0.5..-0.9 % (after 12) | medium | low (guard) |
| 18 | `Replay` without hash maps and shared_ptr counting per buffer | render | RT -0.8..-1.2 % | small | low |
| 19 | Critical sections inline at the call sites; `RtlLeaveCriticalSection` as a raw export | codegen + runtime, bug fix | G -0.6..-0.9 % | medium | low |
| 20 | NEON vertex / index conversion paths | render | RT -0.7..-1 % | small | low |
| 21 | Kernel waits without `dynamic_cast`/`strcmp`, profiler counters and TLS reads | runtime, bug fix | G -0.5..-0.8 % | small | very low |
| 22 | Guest-side D3D hooks raw, `Active()` / `State()` inline | render (guest side) | G -0.5..-0.8 % | small | low |
| 23 | Pooled render-queue commands: no heap allocation per clear, resolve or command-buffer run | render | RT -0.5 %, G -0.3 % | small | low |
| 24 | Native reverb: delay lines without block `memmove`, scratch without `thread_local` | native | G -0.4..-0.6 % | small | low (its own verify) |
| 25 | Kernel export shim: no full compiler barrier after value-returning exports | runtime, bug fix | G -0.2..-0.3 % | small | very low |

---

## 3. The proposals

### 1. Dirty pages marked by the generated stores, instead of re-hashing every cached stream after each kick

**Kind:** codegen + render thread. **Saving:** RT -20..-23 %, guest +1.5..2.5 % of a core; net ≈ -13..-15 % of a core.

**Today.** Every guest store is a plain byte-swapped store (template `sdk/resources/templates/codegen/pch_h.inja`,
expanded in `app/generated/renut_pch.h`):

```cpp
// app/generated/renut_recomp.95.cpp, sub_829BAAA0:  stfs f0,212(r11)
REX_STORE_D_U32(r11.u32, 212, temp.u32);   // (*(u32*)REX_ADDR_D(r, d) = __builtin_bswap32(y))
```

The renderer cannot know what the game wrote, so with `banjo_renut_exact_streams` every cached vertex-stream and index
copy is hashed whole on its first use after each kick (`render/draw.cpp:1893` streams, `:1437` indices; `ExactHash` ->
`ContentHash64` -> `PmullHash`). Package 27 measured 22-24 MB hashed a frame on the PC route at 7 kicks a frame; tiled
textures get `SampleHash` once a frame, non-lock-driven linear textures `ContentHash` once a frame
(`render/textures.cpp:1128`, `:774-791`).

**Instead.** A one-byte-per-4-KB-page card table (1 MB for the 4 GB window). The generator emits a mark after each
guest store whose base register is not r1 (it knows the base register; the 43,000 r1-relative sites skip it):

```cpp
extern "C" uint8_t g_rex_cards[1u << 20];
#define REX_CARD(ea) (g_rex_cards[(u32)(ea) >> 12] = 1)      // A57: lsr + strb, no load, no branch
#define REX_STORE_D_U32(r, d, y) \
  ((*(u32*)REX_ADDR_D(r, d) = __builtin_bswap32(y)), REX_CARD((u32)(r) + (d)))
#define REX_STORE_D_U32_STACK(r, d, y) (*(u32*)REX_ADDR_D(r, d) = __builtin_bswap32(y))   // r1-based
```

The same for `stvx`/`stvlx`/`stvrx`, `REX_MEMCPY_CONST`/`REX_MEMSET_CONST`, `dcbz`, the compare-and-swap, the localized
copies' `BANJO_HOT_STORE_*`, and a `RexCardRange(addr, len)` call in every host writer of guest memory: the SDK's
memcpy/memset exports, file reads, the XMA output, the hand natives, Renut's own write-backs (memexport results).

Render side, on demand (no table scan): each page has a render-thread-owned epoch.

```cpp
// a page is clean for a copy proven at epoch E if its card is 0 and its epoch <= E
bool PagesClean(u32 va, u32 bytes, uint64_t proven) {
  for (u32 p = va >> 12; p <= (va + bytes - 1) >> 12; ++p) {      // + the 0xA/0xC/0xE alias pages of p
    if (__atomic_load_n(&g_rex_cards[p], __ATOMIC_RELAXED) && __atomic_exchange_n(&g_rex_cards[p], 0, __ATOMIC_ACQ_REL))
      g_page_epoch[p] = ++g_card_epoch;
    if (g_page_epoch[p] > proven) return false;
  }
  return true;
}
// draw.cpp, BindStreams:
if (cached->proven_flush != g_flush_serial) {
  if (PagesClean(src.cpu_address, bytes, cached->proven_epoch)) cached->proven_flush = g_flush_serial;   // no hash
  else { /* today's ExactHash path */; cached->proven_epoch = g_card_epoch; }
}
```

**Why identical.** The check answers the same question as the hash ("did the guest change these bytes since the copy
was proven?") from a superset of the evidence: any write marks the page, so a clean page means unchanged bytes; a
dirty page falls back to today's hash. Marks and data are stored in program order before the kick's release, and the
render thread acquires the kick before checking, so a write before the kick is always seen; a write racing the check
is no worse than today's hash reading half-written data. Physical aliases (0xA0000000/0xC0000000/0xE0000000 views of one
page) are checked at the reader. Streams in thread-stack ranges are not cached (r1 stores skip the mark). Guard: a
PC verify mode keeps hashing and counts "pages clean but hash differs" by address, which names any missed writer; it
must stay 0 on the intro route and a level before the switch is defaulted on. No page faults or write-watch are used.

**Evidence.** `PmullHash` 25.5 % of the render thread (1,318 samples); the return addresses sampled while it runs
attribute about 1,276 of them to calls from `ExactHash`. Texture hashing adds more (attribution blurred because `textures.cpp` changed).

**Estimate.** Hashing drops to the dirty pages (dynamic buffers become `dynamic` after `kMaxCacheDrops` and are not
checked): -20..-23 RT %, about 6 ms of render-thread time per frame. Cost: ~205,000 non-stack store sites x 2
instructions ≈ +5.5 % static instructions; stores do not stall, so ≈ +2-3 % of recompiled time, +1.5-2.5 % of a core
spread over the guest threads (measure with `tools/arm_cost_census.py` before the console).

**Origin.** sasrt-nx's `sasrt_cpu_index_no_fingerprint` (no hash per draw, generation rule) and futari/sa2's vertex
dedupe; Banjo cannot prove a generation rule (27c), the cards make it provable. **Effort/risk:** large; a missed host
writer gives stale geometry, which the verify mode and a poison test (zero the copy on a clean-card hit in a PC build)
expose.

### 2. Texture-binding memo per fetch slot

**Kind:** render thread. **Saving:** RT -5..-7 %.

**Today.** For every draw and every fetch slot the shader reads, `FillTextures` (`render/draw.cpp:1285`) calls
`BindTextureLocked` (`render/textures.cpp:976`) and `SamplerSlotLocked` (`:1474`): read the guest header, decode it
(`TextureFetch::FromHeader`), `MaskedDesc`, compare with the device's fetch constant, `ResolvedAlias`, the `TextureSlot`
memo, `DepthWordsSource`, the per-frame content check, `TransitionTextureLocked`; then the sampler lookup.

**Instead.**

```cpp
struct SlotMemo { u32 va, device[6], header[6]; uint64_t frame, epoch; TextureBinding b; u32 sampler; HostTexture *t; };
SlotMemo g_slot_memo[32];
// FillTextures, per slot r:
SlotMemo &m = g_slot_memo[r];
if (m.frame == g_frame && m.epoch == g_texture_epoch && m.va == va &&
    !memcmp(m.device, fetch.dword, 24) && !memcmp(m.header, header->Format.dword, 24)) {
  if (m.t) TransitionTextureLocked(m.t, SHADER_READ);   // no-op when already there
  binding = m.b; sampler = m.sampler;
} else { binding = BindTextureLocked(va, fetch); sampler = SamplerSlotLocked(fetch, binding.lod_scale); m = {...}; }
```

`g_texture_epoch` is bumped by everything the slow path reads besides its arguments: a texture created, retired or
uploaded, a resolve target set (`g_resolve_targets`), depth words made, eviction.

**Why identical.** The slow path is a function of (va, device fetch words, header words, renderer state); the memo is
keyed by all of it and limited to one frame, so the first bind of a frame still runs the per-frame content check. Its
side effects on a hit are idempotent within the frame (`used_frame`), or diagnostics (counters, trace lines) kept on
the hit path. Check: a PC build that runs both and compares bindings.

**Evidence.** `BindTextureLocked` 5.2 %, `SamplerSlotLocked` 1.4, `TextureGuestExtent` 1.2, `FromHeader` 1.0,
`FromGuest` 0.4, `TextureSlot` 0.3 RT %, plus `GetFormatInfo` under them. About 2,000 draws a frame rebind the same
few hundred textures.

**Origin.** marathon-nx "Sampler cache" and "Redundant render and sampler states"; Unleashed "Render and sampler
states the render thread already has are not sent again". **Effort/risk:** medium / low (a missing epoch bump binds a
stale texture: the compare build finds it).

### 3. Recording fast path: `NewOp` without the global lock and map lookups, delta snapshots for immediate draws

**Kind:** render thread's feeding side (guest threads). **Saving:** G -3.5..-4.5 %.

**Today.** Every recorded op (`guest/command_buffers.cpp:318`) takes `g_record_mutex`, runs `FlushDevicePending`, looks
up `g_buffers[command_buffer]` and `g_recording_devices[device_va]` (two `unordered_map`s), compares up to 4 KB of plain
device state with the shadow (`ChangedChunks`) and appends to a `std::vector` arena that grows by reallocation.
Immediate draws copy the whole 12,712-byte device (`backend/queued.cpp:103`):

```cpp
capture->state.assign(bytes, bytes + d3d::kDeviceSnapshotSize);   // 0x31A8 bytes per draw
```

**Instead.** (a) A per-thread one-entry cache {device_va, command_buffer, generation} -> (buffer, set); the lock is
taken only when the cache misses (generation bumped when either map changes) or an atomic count of
`g_pending_vertices` is non-zero. (b) The arena reserved from the buffer's size in the previous frame, so `insert` never
moves it. (c) Immediate draws recorded like ops: the changed plain chunks (as `ChangedChunks`) plus the `m_Pending`
constant groups the capture clears anyway; the render thread keeps a running device per device and applies the delta
(`ApplyState`), as `Replay` does for command buffers.

**Why identical.** (a) and (b) change bookkeeping only. (c) gives the render thread the same device bytes: unchanged
chunks are the previous capture's, changed ones are copied, and float constants follow `m_Pending`, which Renut already
trusts for command-buffer ops (`op.vs_dirty`). Check: a PC build that also keeps the full snapshot and memcmps it with
the rebuilt one at each draw.

**Evidence.** `NewOp` 3.0 G % (13.4 % of drawing thread 67800EA0, 19.5 % of 6A400EA0); half of the guest threads'
`memmove` is entered from `NewOp`, another 11 % from `Capture`; `pthread_mutex_unlock` and `std::unordered_map::
operator[]` are entered from `NewOp`; main thread `memmove` 6.7 %.

**Origin.** Unleashed/marathon-nx "Sparse constant copies", "Zero-copy batches", "Queue token". **Effort/risk:** medium /
low-medium (the running device must be per device and in queue order).

### 4. Memexport bookkeeping memoised per draw

**Kind:** render thread. **Saving:** RT -4..-5 %.

**Today.** While any export is pending, which is most of the frame (particles), every draw runs
`DrawNeedsExportFlush` (`render/draw.cpp:2552`: every declared stream's fetch constant and every texture/vertex fetch
through `PendingExportSlots`), then `ConvertExportedStreamsLocked` (`:2908`, the streams again) and
`GatherMemExportStreams` for VS and PS (`:2298`), from `DrawStateLocked` (`:3177`):

```cpp
while (AnyPendingExports()) { const ExportRead read = DrawNeedsExportFlush(device, call, *vs, ps, ...); ... }
```

**Instead.** An export epoch (bumped when an export is recorded, completes or its slot retires) and a small memo keyed
by (declaration, VS/PS entries, the two words of each used vertex fetch constant, the base pages of the fetched
textures, the draw's first/count): on a hit with the same epoch the result is "none" and both walks are skipped; on a
miss, today's code. `GatherMemExportStreams` returns at once when the entry's `memExportConstants` words are all zero
(a precomputed flag).

**Why identical.** The two walks are pure functions of those inputs and of the pending-export set, whose every change
bumps the epoch. Check: PC build compares memo hits with a real walk.

**Evidence.** `DrawNeedsExportFlush` 2.6, `ConvertExportedStreamsLocked` 2.3, `GatherMemExportStreams` 1.6,
`PendingExportSlots` 0.5 RT % (draw.cpp attribution approximate), plus `State()` calls entered from them.

**Origin.** Banjo-specific. **Effort/risk:** medium / low.

### 5. Tuning getters and statistics counters latched

**Kind:** render thread, bug fix. **Saving:** RT -3..-4 %, G -0.4 %.

**Today.** Dozens of out-of-line calls per draw, each with a static guard and often an atomic load, plus atomic
read-modify-writes for statistics (on the A57 without LSE each is an `ldxr`/`stxr` loop):

```cpp
// backend/backend.cpp:261 (called through inline IsDeko3D()/IsVulkan()/UsesVulkanBindings())
Backend ActiveBackend() { static const Backend backend = ResolveBackend(); return backend; }
// banjo_renut_tuning.cpp:620 / :1088
bool DepthOnlyOn() { static const bool on = ...; return on && g_ab_off.load(std::memory_order_relaxed) != 11; }
void CountSharedConstants(bool reused) { (reused ? g_shared_reused : g_shared_uploaded).fetch_add(1, ...); }
// plume_deko3d_commands.cpp:320,353,369: GetDeko3DStats().draws / pipelineBinds / stateBinds .fetch_add per draw
```

**Instead.** One `struct RenutFlags` (backend, every `*On()`, the A/B word) filled at start and copied at each frame
start; header-inline accessors read it. Statistics in render-thread-owned plain `uint64_t`s, added to the shared
atomics once a frame (plume's `Deko3DStats` the same way). Same for `banjo::renut_gpu::Active()`
(`banjo_renut_select.cpp:18`) on the guest side.

**Why identical.** The values are read-only after start except `g_ab_off`, a diagnostic A/B switch, which then changes
at a frame boundary instead of mid-frame. Counters only feed reports.

**Evidence.** `ActiveBackend` 1.64, `CountSharedConstants` 0.93, `State()` 0.41, `PointMinMaxRadiusOn` 0.41,
`Deko3DCompileCore` 0.23, `Deko3DVsOutputsOn` 0.19, `PushPointOnly` 0.17, `gpu_profile::CountDraw` 0.27,
`DepthOnlyOn`, `CountExactCheck`, `ResolveTraceOn` RT %; `Active()` 0.48 G %.

**Origin.** sa2-nx's SDK list items 5 and 11 (settings and profiler counters as out-of-line calls); sasrt-nx
`hook_counter_ctx`. **Effort/risk:** small / very low.

### 6. Pipeline lookup: memo before hashing, resolved fallbacks for pending and failed specialisations

**Kind:** render thread, bug fix. **Saving:** RT -3..-3.5 %.

**Today** (`render/pipeline.cpp:891`). A one-entry memo, then `HashKey` (XXH3 over the ~190-byte key), a bucket in
`g_pipelines`, `memcmp`. A specialised key (bools, alpha function) that is pending or has failed misses that memo on
**every** draw, because `g_last_key` only ever holds the key of a built pipeline. Pending: `HashKey`,
`g_pipelines[hash]` (which inserts an empty bucket), `g_spec_pending.find` and a key `memcmp`. Failed: the same, then
`g_spec_failed.contains`, an erase, an insert, and the recursive lookup of the generic key with a second XXH3.

**Instead.** A 64-entry direct-mapped memo indexed by a cheap mix of (vs, ps, layout_hash, blend/depth words), full key
`memcmp` on hit, holding whatever pipeline the slow path returned for that key: built, generic-for-pending (tagged with
`g_prebuild_epoch`, so a finished build is picked up at the next epoch) or generic-for-failed.

**Why identical.** The memo returns the pointer the slow path returns for the same key and epoch; failed
specialisations stay failed (`g_spec_failed` is never cleared today either).

**Evidence.** `GetPipelineLocked` 1.51, `XXH3_64bits` entered from it (111 weighted leaf samples ≈ 2 %), `memcmp` from
it, `std::_Hashtable<unsigned long, unsigned long, ...>` (a u64 set: `g_spec_failed`) 0.39, `MakeKey` 0.37 RT %.

**Origin.** marathon-nx "Pipeline lookup cache" (keyed by the raw state). **Effort/risk:** small / low.

### 7. Hot-edge inline copies, and the toml-comment "hook" bug

**Kind:** codegen, bug fix. **Saving:** G -1.5..-2.5 %.

**Today.** Generated files are compiled separately, so a call to another file is a real call with the ctx hand-over
(arguments stored to ctx, callee frame, reloads). `tools/inline_leaves.py` (step 32) copies only leaves of at most 12
guest instructions. The hottest leaf edges in the profile are larger and cross files:

| Caller (file) | Callee (file) | Callee | Leaf-callee weight |
|---|---|---|---|
| `sub_823B59C0` (.72) | `sub_823B4C78` (.175) | 45 instructions, leaf | 368 |
| `sub_82951B78` (.165) | `sub_82CCD1C8` (.117) | 51, one call | 361 |
| `sub_82240178` (.50) | `sub_82240200` (.28) | 42, leaf | 349 |
| `sub_823AA1A0` (.110) | `sub_823A8CC0` (.87) | 112, leaf | 169 |
| `sub_826F3508` (.129) | `sub_82714358` (.94) | 18, leaf | 168 |
| `sub_823A9730` (.106) | `sub_823A9908` (.1) | 24, leaf | 137 |
| `sub_82C93908` (.82) | `sub_82C8E038` (.142) | 73, leaf | 131 |

Over the tree: 25,616 cross-file call sites to 3,250 unhooked leaves of 13-128 instructions, 54,545 more to small
functions with at most two calls. Prologue/epilogue and spill stores are ~9.5 % of recompiled samples.

**Bug found on the way.** `tools/direct_calls.py` strips comments only from C/C++ files; `app/renut_config.toml` is read
whole, and its comment at line 55 names `sub_8294EF68, sub_823B59C0, sub_823B4C78, sub_82CE9250`. 24 addresses are
"hooked" only by toml comments, leaving 99 call sites on the weak `noinline` alias (56 to `sub_82CE9250`, 3 to
`sub_823B4C78`), which also keeps them out of step 32. Example, `renut_recomp.72.cpp`:

```cpp
// bl 0x823b4c78
sub_823B4C78(ctx, base);          // weak alias: never inlined, excluded from inline copies
```

**Instead.** (a) `direct_calls.py`: drop `#` comments from tomls and CMakeLists before scanning (as peggle2-nx did for
C++ comments). (b) `inline_leaves.py --hot-edges app/config/hot_edges.txt`: the edges the console profile names
(leaf-callee view of `tools/switch_hot_stacks.py`), callees up to 128 guest instructions (leaves, or with at most two
calls), `static inline` copies in the caller's file exactly as step 32 makes them:

```cpp
static inline void inl_sub_823B4C78(PPCContext& __restrict ctx, uint8_t* base) { /* the same statements */ }
...
REX_INL(sub_823B4C78)(ctx, base);
```

**Why identical.** The same statements on the same ctx and memory; hooks respected (only unhooked `__imp__` calls are
copied); `REX_INLINE_LEAVES_OFF` restores the calls. Alternative with the same effect: generated files as contiguous
address ranges (package 27 idea 15), which needed a console A/B.

**Evidence/estimate.** The edges above alone are ~2.5 G % of leaf-callee weight; inlining removes the call, the
callee's frame and part of the hand-over (10-20 % of callee+call cost): -1.5..-2.5 G %. Code grows; limit to the
listed edges.

**Origin.** Banjo's own step 32 and docs ("flattening the call chain ... the next idea, not attempted"); the comment
fix is peggle2-nx's for C++ files extended to tomls. **Effort/risk:** medium / low.

### 8. Render-target, shader and declaration lookups memoised per draw

**Kind:** render thread. **Saving:** RT -2..-2.5 %.

**Today.** `GatherTargets` calls `GetHostSurfaceLocked` (`render/surfaces.cpp:257`) for each bound colour target and the
depth target, every draw: `SurfaceRenderScale` (graphics options), `TargetKey`, `g_surface_keys.try_emplace` and
`g_targets[key]` (two hash maps); then `GetFramebufferLocked`. `FindShaderLocked` runs twice a draw.

**Instead.** Per target slot: (surface_va, the descriptor's Size / SurfaceInfo / Format words, resolution epoch, targets
epoch) -> `HostSurface*`; the targets epoch is bumped when a target is created, grown, retired or a tiling extent
changes. Last VS and PS address -> record, as `g_last_declaration` already does for declarations
(`render/draw.cpp:3145`).

**Why identical.** Same key, same function: the memo returns what the lookup returns until something it reads changes.
The descriptor words that the slow path stores into the surface (`size_word`, `info_word`, `format_word`) are part of
the key, so the stores are repeats.

**Evidence.** `GetHostSurfaceLocked` 1.55, `SurfaceRenderScale` (88 weighted leaf samples from it), `FindShaderLocked`
0.85, `GetFramebufferLocked` 0.58, `GetHostShader` 0.41 RT %.

**Origin.** sa2-nx/sasrt-nx `ring_identity_memo` / `edram_memo` (state memos per change, not per draw). **Effort/risk:**
small / low.

### 9. Float and shared constants by dirty groups from a host-order shadow

**Kind:** render thread. **Saving:** RT -2..-2.5 %.

**Today.** `UploadFloatConstants` (`render/draw.cpp:1186`): outside a tracked replay it compares every register the
shader reads with the last upload, and on a change byte-swaps every used group into the ring. Every draw also
`memset`s a `SharedConstants`, refills it and `memcmp`s it with the last one (`DrawStateLocked`, the dedupe).

**Instead.** A host-order shadow of the 2 x 256 registers per device, updated only for the groups the ops' and captures'
`vs_dirty`/`ps_dirty` name; "reuse" becomes `(used_groups & dirty_since_upload) == 0`; an upload copies the used groups
from the shadow (no swap). The shared block is built in place in the bound copy, tracking which words changed, so the
`memcmp` goes.

**Why identical.** The uploaded bytes are the same registers, swapped once at change time instead of at each upload; the
dirty masks are the ones Renut already trusts for command-buffer replays. Check: PC build compares the upload with
today's.

**Evidence.** `UploadFloatConstants` 3.9 RT %; `memcmp` entered from `DrawStateLocked` (53 weighted); `memset` 0.35.

**Origin.** Unleashed "Shader constants kept in host byte order (swap on set, copy on upload)", "swaps, compares and
stores constants in one pass"; marathon-nx "Sparse constant copies". **Effort/risk:** medium / low.

### 10. Vector any-mode forms: short VMX runs in FPU mode behind a denormal guard

**Kind:** codegen. **Saving:** G -1.2..-1.8 %.

**Today.** Every VMX float instruction switches the host to flush mode, every scalar one back
(`sdk/src/codegen/builders/vector.cpp:93`, `builders/context.cpp:495`). Mixed straight-line maths toggles twice per
short run, each toggle a 43.6 ns `msr fpcr`:

```cpp
// app/generated/renut_recomp.72.cpp, sub_823B59C0
// vmaddfp v12,v5,v13,v0
ctx.fpscr.enableFlushModeUnconditional();
simde_mm_store_ps(ctx.v12.f32, simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v5.f32), ...), ...v0));
// vmaddfp v0,v9,v13,v0
...
// fsubs f6,f6,f7
ctx.fpscr.disableFlushModeUnconditional();
```

2,016 unconditional toggles in the tree; 283 enable/disable pairs enclose at most six VMX float instructions.

**Instead.** Where a VMX run between scalar code has at most N (4-6) float instructions, keep FPU mode and emit guarded
forms:

```cpp
// rex::ppc (diet_ops.h): the flush-mode result of vmaddfp, computed with the mode off
inline simde__m128 VMaddfpAnyMode(simde__m128 a, simde__m128 b, simde__m128 c) noexcept {
  const float32x4_t p = vmulq_f32(a, b), r = vaddq_f32(p, c);          // simde's order: mul, then add
  const uint32x4_t tiny = Denormal(a) | Denormal(b) | Denormal(c) | TinyOrMin(p) | TinyOrMin(r);
  if (vmaxvq_u32(tiny)) [[unlikely]] return detail::VMaddfpFlushSlow(a, b, c);   // msr FZ=1, same expression, back
  return r;
}
// Denormal(x): 0 < |x| < FLT_MIN;  TinyOrMin(x): 0 < |x| <= FLT_MIN
```

**Why identical.** FZ changes a result only through a denormal input or a tiny result (AArch64 flushes on tininess
before rounding, so a result rounded up to exactly FLT_MIN is also sent to the slow path, as is any tiny intermediate
product). Zeros, normals, infinities and NaNs (FPCR.DN unchanged) give the same bits in either mode; anything else runs
the original expression in flush mode. Unit test against the plain flush-mode expression on random and edge values,
like the existing any-mode helpers' tests in `sdk/tests`.

**Evidence.** 147 sampled PCs (3.4 % of recompiled samples) next to `msr fpcr`; `sub_823B59C0` 17 of its 47 samples,
`sub_823B4C78` 7 of 47, `sub_8294EF68` 17 of 119, `sub_82CE9250` 6 of 38, `sub_82949790` 6 of 66 (the functions
`app/renut_config.toml` names as the scalar/vector alternators).

**Estimate.** About two thirds of those toggles sit around short runs: -1.2..-1.8 G %. The guard costs ~15 NEON
instructions per guarded op against ~88 cycles of toggles.

**Origin.** marathon-nx "MOPP query in one FPCR mode" (its vector arithmetic in the scalar mode: 43 `msr fpcr` out of
the hot path), sasrt-nx's `fp_mode_lookahead` (the scalar side of the same idea). **Effort/risk:** medium / low.

### 11. Vertex-stream and index binding memo

**Kind:** render thread. **Saving:** RT -1.5..-2 %.

**Today.** Per draw and stream, `BindStreams` (`render/draw.cpp:1843`) calls `GetStreamSource` (guest vertex-buffer
lookup through the device words), builds a `StreamKey` and looks it up in `g_stream_memo` / `g_stream_cache` (a hash of
the key); indices go through `g_index_memo.Get` in `CachedIndexDraw` (`:1427`).

**Instead.** A per-slot memo of (the stream's two fetch-constant words, layout_hash, first, count, endian) ->
(`CachedStream*`, bound view); same for indices (address, count, primitive, endian). On a hit, only the
`proven_flush` / proposal-1 check and the bind remain.

**Why identical.** `GetStreamSource` and the key are pure functions of those words; the cache entry pointer is stable
until erased (erasure clears the memo).

**Evidence.** `BindStreams` 2.3, `GetStreamSource` 0.48 (+113 weighted from `BindStreams`), the `StreamKey` hash table
0.46, `PrepareGeometry` 1.72 RT %.

**Origin.** sasrt-nx `vertex_no_fingerprint` / `index_no_fingerprint` lookups. **Effort/risk:** small / low.

### 12. Locals opt-out list re-measured with by-value vectors and console weights

**Kind:** codegen, bug fix. **Saving:** G -0.8..-1.5 %.

**Today.** `app/config/locals_optout.txt` (296 functions) was written on 2026-10-16 at 04:58, before
`REX_VECTOR_LOCALS_BY_VALUE` existed (package 21b, ~09:45): it judged loop and leaf locals with vector locals as
references into ctx, so vector-heavy functions lost. It is also a static loop-weighted cost, not a measured one. The
hottest opted-out function, the drawing threads' culling test, keeps its registers in ctx inside the loop
(`app/config/locals_optout.txt: leaf sub_829A6DE8`); its loop in the ELF:

```
stp  x19, x12, [x2, #48]      // ctx.r6, ctx.r7 every iteration
str  q30, [x2, #752]          // ctx.v10 every iteration
stp  q30, q29, [x2, #704]     // ctx.v7, ctx.v8
ldr  q28, [x2, #736]          // ctx.v9
```

**Instead.** Re-run `tools/locals_choice.py` with `-DREX_VECTOR_LOCALS_BY_VALUE` on its compile lines, and for the
functions in the console profile decide by `tools/hot_arm_compare.py` block counts instead of the static weight; a new
list needs a fresh codegen (edit the renut_config.toml comment, as documented).

**Why identical.** Loop and leaf locals are exact by construction (the existing tools); only the per-function choice
changes.

**Evidence.** Opted-out functions hold 4.93 % of guest samples: `sub_829A6DE8` 1.70 % (11.6 % of drawing thread
67800EA0; 30+ of its 120 samples on ctx vector loads/stores), `sub_82BDAED0` 0.96, `sub_829A1D88` 0.84 (2.8 % of the
main thread), `sub_82959230` 0.25.

**Origin.** Banjo (stale decision found here). **Effort/risk:** small / very low.

### 13. plume deko3d binding: sub-state diffing, cached shader stages, no per-bind atomics

**Kind:** render thread. **Saving:** RT -1.5..-2 %.

**Today** (`app/thirdparty/plume/plume_deko3d_commands.cpp:334`). On every pipeline change: `GetDeko3DStats()` (out of
line) and an atomic add, `dkShaderGetStage` (an out-of-line deko3d call) per shader to rebuild `want[]`, and when the
state hash differs, all of rasterizer, multisample, colour, colour-write, blend (+ replayed words), depth-stencil,
stencil, depth bias, vertex attributes and vertex buffers are rebound, whichever of them changed.

**Instead.** Stages stored in the pipeline at creation (`p->stageSlot[i]`); a per-sub-state hash in the pipeline,
compared with the bound ones, binding only the parts that differ; statistics as in proposal 5.

**Why identical.** The GPU state after the binds is the same: a sub-state equal to the bound one is a redundant
command. Check: the command-buffer bytes of a frame with and without the diffing decode to the same final state
(debug compare on PC with the deko3d back end, or a Ryujinx capture).

**Evidence.** `setPipeline` 1.47, `dkShaderGetStage` 0.33, `dkCmdBufBindShaders`, `dkCmdBufBindRasterizerState` 0.21,
`dkCmdBufBindVtxAttribState` 0.17, `bindTextures` 0.62, `bindGraphicsBuffers` 0.70, `bindSetBuffers` 0.54 RT %; ~6 % of
the thread in plume + deko3d together.

**Origin.** Unleashed/marathon-nx "plume: redundant binds filtered". **Effort/risk:** small / low.

### 14. Upload ring: no clean+invalidate per unmap

**Kind:** render thread. **Saving:** RT -1..-2 %.

**Today.** The upload buffers are CPU-cached; each chunk's unmap calls `dkMemBlockFlushCpuCache`
(`plume_deko3d.cpp:451-461`, from `render/upload_ring.cpp:97`), which runs `armDCacheFlush` (`dc civac`: clean **and
invalidate** each line), so the ring's next pass re-allocates every line.

**Instead.** (a) The write-once streams (float constants, shared constants, fetch data) from a second ring in
`DkMemBlockFlags_CpuUncached` memory (write-combined; written whole, in order, never read back: the dedupes compare CPU
copies, not the ring), so no maintenance at all; (b) the cached ring cleaned with `dc cvac` (libnx `armDCacheClean`)
instead of `civac`.

**Why identical.** Cache maintenance only; the GPU reads the same bytes. Audit that no code reads ring memory (a read of
uncached memory would be very slow, not wrong).

**Evidence.** `armDCacheFlush_L0` 2.42 RT %, entered from `dkMemBlockFlushCpuCache`.

**Origin.** Banjo / deko3d specifics (Unleashed's comment on write-combined uploads). **Effort/risk:** small / low-medium
(performance has to be measured on the console: write-combined partial writes are slow if a path writes 4 bytes at a
time).

### 15. Single-precision bit forwarding through `fmr` / `fneg` / `fabs` / `fnabs`, in any mode

**Kind:** codegen. **Saving:** G -0.6..-1 %.

**Today.** `fp_copy_pairs` (`builders/context.cpp:556`, `FloatCopyLoadOf`) forwards the loaded bits only when nothing but
integer instructions and other `lfs`/`stfs` stand between the load and the store, and only in VMX mode. The culling loop
negates in between and pays two range checks and two conversions:

```cpp
// renut_recomp.95.cpp, sub_829A6DE8 (loop):  lfs f0,4(r11) ; fneg f0,f0 ; stfs f0,-112(r1)
temp.u32 = REX_LOAD_D_U32(r11.u32, 4);
f0.f64 = rex::ppc::SingleToDoubleAnyMode(temp.u32);     // and+sub+cmp+b.ls, fmov, fcvt
REX_FNEG(f0, f0);
temp.f32 = rex::ppc::DoubleToSingleAnyMode(f0.f64);     // fmov, and+sub+cmp+b.cc, fcvt, fmov
REX_STORE_D_U32(ctx.r1.u32, -112, temp.u32);
```

(the ELF's hottest instruction in that function is this `fcvt s0, d0` after its `b.cc`, 13 of 120 samples).

**Instead.** Track, per float register within a block, "holds the widening of 32-bit pattern P, sign op S"; at `stfs`
store the pattern:

```cpp
fb0.u32 = REX_LOAD_D_U32(r11.u32, 4);
f0.f64 = rex::ppc::SingleToDoubleAnyMode(fb0.u32);      // kept only if something else reads f0 (GCC drops it)
REX_FNEG(f0, f0);
temp.u32 = rex::ppc::QuietSingleBits(fb0.u32) ^ 0x80000000u;   // fabs: & 0x7FFFFFFF, fnabs: | 0x80000000
REX_STORE_D_U32(ctx.r1.u32, -112, temp.u32);
```

**Why identical.** The existing copy form already defines lfs -> stfs as `QuietSingleBits(bits)` (the host conversion
quiets a signalling NaN, keeping the payload); the sign operations flip, clear or set bit 31 of the double, which maps
to bit 31 of the single for every value including NaNs, and the narrowing back is exact for a widened single in any
mode. Any other write to the register ends the tracking; labels and calls end it as in `FloatCopyLoadOf`.

**Evidence.** In the tree, 16,226 `stfs` store a value an `lfs` of the same block put in the register (another float
write to it ends the scan), of which 901 take the copy form today; 340 more go through sign operations. Samples on
the any-mode conversion branches: 104 (2.4 % of recompiled samples): `sub_8294EF68` 32, `sub_829A6DE8` 19,
`sub_82949790` 12.

**Origin.** sa2-nx candidate 4 / Banjo's `fp_copy_pairs`, generalised. **Effort/risk:** small / low (extend the unit
test of the copy form).

### 16. Hand-written native LZX (XCOMPRESS) block decoder

**Kind:** native hot function. **Saving:** G -0.8..-1.2 % while the game streams (loading screens and level streaming).

**Today.** `rex_fast_decode_aligned_offset_block_XCOMPRESS__YAJPAUt_decoder_context_1_JH_Z` (266 guest instructions,
the XDK's LZX decoder) runs recompiled: bit-buffer refills with byte-swapped loads, per-symbol table walks in guest
memory.

**Instead.** A hand-written native of that function (and `rex_make_table_XCOMPRESS`) on the same `t_decoder_context`
struct in guest memory, with the bit buffer and window pointers in host registers, hooked with the existing guard (native
against original on copies, written ranges compared) and a code-hash check.

**Why identical.** LZX decoding is deterministic: same output bytes and the same context fields written back; the guard
proves it per call during its guarded calls.

**Evidence.** 127 samples, 1.80 G % (the largest single recompiled library function in the gameplay windows).

**Origin.** marathon-nx "Native inflate" (zlib), futari-nx `futari_native_lzss`. **Effort/risk:** large / medium.

### 17. Hand-written native of the culling test `sub_829A6DE8`

**Kind:** native hot function. **Saving:** G -0.5..-0.9 % after proposal 12.

**Today.** A box-versus-six-planes test (two loops of six: load plane, select the box corner per lane with `vsel`,
`vmsum4fp128`, compare, accumulate masks), 11.6 % of the main drawing thread, built from `lvlx`+`vrlimi128` unaligned
loads through table lookups and ctx vector traffic.

**Instead.** A native that keeps the planes and corners in NEON registers, uses one unaligned load per float3, and
computes the dot products with exactly simde's `simde_mm_dp_ps(..., 0xFF)` NEON sequence (same multiplies and the same
pairwise additions, so the same bits), writing back the registers its callers read (the guard's skip set as for the
other natives).

**Why identical.** Same IEEE operations in the same order; the guard compares registers and memory.

**Evidence.** 120 samples, 1.70 G %; do proposal 12 first (it may halve this) and re-measure with
`tools/hot_arm_compare.py`.

**Origin.** sa2-nx's hand natives (matrix, vertex chunks); marathon-nx's native box-corner projection. **Effort/risk:**
medium / low.

### 18. `Replay` without hash maps and shared_ptr counting per buffer

**Kind:** render thread. **Saving:** RT -0.8..-1.2 %.

**Today.** `Replay` (`guest/command_buffers.cpp:411`) takes a `std::unordered_map<u32, std::shared_ptr<HostCommandBuffer>>`
and looks buffers up (and copies shared_ptrs: atomic increments and decrements) per run; `SetReplayPredicationLocked`
is called per draw.

**Instead.** Buffers in a flat vector indexed by a small id assigned at recording; raw pointers inside a replay (the
owning shared_ptr held once by the run); predication recomputed only when the run's mask or the draw's own mask
changes.

**Why identical.** Ownership and order unchanged; lookups and refcounts only.

**Evidence.** `Replay` 1.74 RT % self; `SetReplayPredicationLocked` 73 weighted leaf samples from it; `_Map_base<unsigned
int, ...HostCommandBuffer...>` lookups on the guest side too.

**Origin.** Banjo. **Effort/risk:** small / low.

### 19. Critical sections inline at the call sites; `RtlLeaveCriticalSection` as a raw export

**Kind:** codegen + runtime, bug fix. **Saving:** G -0.6..-0.9 %.

**Today.** `bl RtlEnterCriticalSection` goes to the raw export (`sdk/src/kernel/xboxkrnl/xboxkrnl_rtl.cpp:500`), but
`RtlLeaveCriticalSection` is a plain `REX_EXPORT` (`:715`): `HostToGuestFunction` translation, and
`assert_true(cs->owning_thread == XThread::GetCurrentThread()->guest_object())` (a thread-local lookup) if asserts are
compiled in.

**Instead.** (a) Leave as a raw export taking r3 and the owner from r13 like Enter. (b) The generator emits the
uncontended fast paths inline at each call site of the import:

```cpp
// RtlEnterCriticalSection(r3)
{ auto* cs = GuestPtr<X_RTL_CRITICAL_SECTION>(ctx.r3.u32); const u32 me = PcrThread(ctx, base);   // from r13
  if (cs->lock_count == -1 && REX_GUEST_CAS(&cs->lock_count, -1, 0)) { cs->owning_thread = me; cs->recursion_count = 1; }
  else __imp__RtlEnterCriticalSection(ctx, base); }                                                // today's path
```

and for Leave the `--recursion_count != 0` and "no waiter" paths inline, the rest through the export.

**Why identical.** The same memory writes, in the same order, with the same atomic semantics as `xeEnterCriticalSection`;
contended, recursive and waiting cases go through the existing code. The owner from the PCR is already the default
(`kernel_critsec_pcr_owner`, PC self-check).

**Evidence.** `RtlEnterCriticalSection_raw` 0.51, `RtlLeaveCriticalSection_entry` 0.34 G %, plus their thunks; the
XHV engine and audio code call them per packet.

**Origin.** sa2-nx SDK list item 12 (thread id from the context), marathon-nx "Lean critical-section leave", "Fast
critical sections". **Effort/risk:** medium / low.

### 20. NEON vertex and index conversion paths

**Kind:** render thread. **Saving:** RT -0.7..-1 %.

**Today.** `ExpandElement<2>` (`render/vertex_format.cpp`), `ConvertVertices`, and `ReadIndices`' 16-bit path
(`render/draw.cpp`, a scalar loop with a swap test per index) convert element by element.

**Instead.** NEON paths for the common cases: 16-bit indices with `vrev16q_u8` 64 bytes a step; 32-bit word streams
with `vrev32q_u8`; the per-element expanders for whole vertices of the common layouts (positions, normals, UVs) with
loads of 16 bytes and `tbl` shuffles.

**Why identical.** Byte moves only (no float arithmetic). Unit test against the scalar converters.

**Evidence.** `ExpandElement<2>` 0.77, `ConvertVertices` 0.33, `DecodeBlockRow` 0.39, part of `PrepareGeometry` 1.72 RT %;
`FlushVertexFillsLocked` 110 weighted leaf samples.

**Origin.** marathon-nx "NEON byte-swapped copies". **Effort/risk:** small / low.

### 21. Kernel waits without `dynamic_cast`/`strcmp`, profiler counters and TLS reads

**Kind:** runtime, bug fix. **Saving:** G -0.5..-0.8 %.

**Today.** Every guest wait goes through `rex::thread::Wait`/`WaitMultiple`, which `dynamic_cast<PosixWaitHandle*>`
each handle (`sdk/src/core/threading_posix.cpp:1835`, `:1909`); libstdc++ on this target compares type names with
`strcmp` in the hierarchy walk. `XObject::Wait` calls `XThread::CheckTitleTermination()` twice
(`sdk/src/system/xobject.cpp:209/211`: a `thread_local` read through `__aarch64_read_tp`), and `ObjectTable::LookupObject`
calls `RexSwitchPerfAdd` per lookup.

**Instead.** `static_cast` (every wait handle on POSIX/Switch is a `PosixWaitHandle`; assert it in PC builds); the
current thread from the guest context or a cached pointer; the per-lookup profiler count compiled out unless the
profiler window is on.

**Why identical.** The same objects and calls; the cast cannot fail on this platform.

**Evidence.** `strcmp` <- `__si_class_type_info::__do_dyncast` 763 and `__vmi_...` 172 weighted; `__dynamic_cast` <-
`rex::thread::Wait` / `WaitMultiple` 146/149; `__aarch64_read_tp` <- `CheckTitleTermination` 234; `RexSwitchPerfAdd` <-
`LookupObject` 192. Together `strcmp`, `__do_dyncast`, `__dynamic_cast` ≈ 0.65 G %.

**Origin.** sa2-nx SDK list items 9 and 11. **Effort/risk:** small / very low.

### 22. Guest-side D3D hooks raw, `Active()` and `State()` inline

**Kind:** render thread's feeding side. **Saving:** G -0.5..-0.8 %.

**Today.** The D3D hooks the game calls per draw (`D3DDevice_DrawIndexedVertices_hook`, `SetTexture`,
`SetStreamSource`, `SsMaxMipLevel`) go through `HostToGuestFunction` translation and call the out-of-line
`renut::gpu::State()` (a function-local static) and `banjo::renut_gpu::Active()` each time.

**Instead.** `REX_HOOK_RAW` forms that read r3-r6 directly; `Active()` and the `VideoState` pointer as plain globals set
at start (inline).

**Why identical.** The same values reach the same code.

**Evidence.** `native::SetTexture` 0.57, `Active()` 0.48, `SetStreamSource` 0.23, the DrawIndexedVertices thunk 0.21,
`SsMaxMipLevel` 0.18 G %; `State()` 181 weighted leaf samples from the draw thunk.

**Origin.** sasrt-nx/sa2-nx raw hooks (critical sections). **Effort/risk:** small / low.

### 23. Pooled render-queue commands: no heap allocation per clear, resolve or command-buffer run

**Kind:** render thread (both ends of the queue). **Saving:** RT -0.5 %, G -0.3 %.

**Today.** Draw captures are pooled (`banjo_renut_capture_pool`), but `QueueClear` builds an `Args` with a
`std::vector<u8> state` of the whole device per clear (`backend/queued.cpp:164`), and `QueueResolve` /
`QueueRunCommandBuffer` capture vectors in their closures: allocated on a game thread, freed on the render thread,
both through newlib's single malloc lock.

**Instead.** The same pool for clears and resolves (or the delta form of proposal 3); command-buffer runs carry an index,
not a copied vector.

**Why identical.** The same bytes reach the render thread.

**Evidence.** `memmove` <- `QueueResolve` 31 and `QueueRunCommandBuffer` 25 weighted on the main thread; `__malloc_lock`
<- `_malloc_r` / `_free_r` 282/294 weighted on guest threads; `_free_r` 0.5 RT %, `operator delete` from render code.

**Origin.** Banjo's own capture pool, extended. **Effort/risk:** small / low.

### 24. Native reverb: delay lines without block `memmove`, scratch without `thread_local`

**Kind:** native hot function. **Saving:** G -0.4..-0.6 %.

**Today.** `banjo_reverb::Execute` (`app/src/cpu/banjo_reverb_native.cpp:1412`) moves 256/257-float blocks with
`memcpy`/`memmove` between stages (`:546`, `:553`, `:1100`) and reaches its scratch through `thread_local`
(`:1403`, a `__aarch64_read_tp` call per access on the Switch).

**Instead.** Ring-buffer indices for the delay lines (no shift of the block), stages writing into their successor's
buffer, the scratch pointer passed down from `Execute`.

**Why identical.** The same samples in the same order; the reverb's own verify compares it with the recompiled
original bit for bit.

**Evidence.** `Execute` 2.85 G % self (19 % of audio thread 46BDCA80); 16 % of the guest threads' `memmove` weight is
entered from it; `__aarch64_read_tp` <- `Execute` 26 weighted.

**Origin.** Banjo. **Effort/risk:** small / low.

### 25. Kernel export shim: no full compiler barrier after value-returning exports

**Kind:** runtime, bug fix. **Saving:** G -0.2..-0.3 %.

**Today** (`sdk/include/rex/ppc/function.h:406`). Every `REX_EXPORT` goes through a `noinline` `HostToGuestFunction`
whose value-returning form ends with `asm volatile("" ::: "memory")` before storing r3, which forces everything the
shim holds back to memory.

**Instead.** Store the return value without the barrier (the store to `ctx.r3` is ordered by the function's return; no
other thread reads ctx), and mark the shim `always_inline` into the `__imp__` export body, so the export is one call.

**Why identical.** The barrier orders nothing that another observer can see: ctx is thread-private and the guest
memory the export wrote is stored before the return in any case.

**Evidence.** Hook thunks 0.71 G %; sa2-nx counted about 1,100 export calls a frame in its game.

**Origin.** sa2-nx SDK list item 10. **Effort/risk:** small / very low.

---

## 4. Ideas looked at and rejected

| Idea | Why not |
|---|---|
| A faster stream hash | `PmullHash` is bandwidth-bound: its samples sit on the loop's `ldp` and the hashed data is out of cache. Only hashing fewer bytes helps (proposal 1). |
| Checking the stream hashes on another core | Package 27c's check worker answered 0 % on the console and was removed in 28b. |
| A per-frame proof of stream contents | Not provable for this game (27c: ~20 guest functions read the fence word directly). |
| Page protection or write-watch for dirty tracking | Switch page faults corrupt NEON registers in 1-4 % of round trips (memory note). |
| Guest memory mapped as GPU memory (zero-copy streams) | The GPU is not coherent with the CPU caches: written lines need cleaning, which needs proposal 1's cards first; real fence timing would also change pacing. A follow-up to 1, not a proposal of its own. |
| Native XAudio mixer (`rex_XAudioMixFrameBufferChannel`, SRC, router) | The guest code is already VMX (`vmaddfp`, `vaddfp`, `lvx128`, `stvx128`): a NEON native would be the same instructions. |
| Scalar `vrsqrtefp`/`vrefp` of splatted dot products (Unleashed round 15) | 1,172 sites, but no vector `fsqrt`/`fdiv` sample in the profile and none in the hot functions. |
| `vrlimi128` as one lane insert | GCC already emits `mov v.s[i], v.s[j]` and `zip2` for the simde blend+permute (checked in `sub_829A6DE8`'s disassembly). |
| Vector-compare record forms (`vcmp*.` -> cr6) with `uminv`/`umaxv` | 358 sites; the `ushl`/`sshl` samples belong to the WMV filters, not to `setFromMask`. |
| VMX shifts by a constant splat as immediate NEON shifts | Only the WMV decoder's filters show them (FMV only, ~0.4 % of recompiled samples). |
| Indirect-call inline caches | 12 samples (0.3 %) on the dispatch sequence; the next promotions are polymorphic (package 27 idea 10). |
| Interpreter dispatch rewrite (`sub_82948928`) | The hot `ldrb` is the opcode fetch at the threaded-dispatch head; the dead jump-table load is already gone (GCC); the loop barrier is required there. |
| Volatile vector registers v0-v13 as call-boundary locals | Remaining stores are read by callees or callers (dead ones are already dropped by step 22); the GPR form of the idea measured +2.8 % (args in registers). |
| Dead stack temporaries of int<->float idioms (`std`/`lfd`) | Not exact: a game may read stack words it never wrote (sa2-nx's finding); needs must-write summaries. |
| Small variable-size `memcpy` inline at call sites | Only ~4 % of the guest threads' `memmove` comes from game code (`sub_829A1D88`); the rest is the renderer (proposal 3), reverb (24) and FFmpeg. |
| Always flush mode, scalar ops through any-mode helpers | Scalar float operations far outnumber the switches; sa2-nx lost even with the past-branch bet. |
| `vmsum4fp128` rewritten | Must match simde's addition order bit for bit (package 27 idea 14). |
| The limiter copy (`n_82B0D0C0`) running its guarded form | Checked: a symbolization artifact of the older ELF; with the matching layout its samples are the fast `Native` form. |
| The record worker | Exists (`banjo_renut_record_worker`, A/B 14); profile 21 ran with it off, so its render-thread figures include recording. Worth an A/B in the next console run, not a new proposal. |
| PGO, LTO, `-O2`, `-fipa-pta`, hot-section linker scripts | Build-side; excluded by the brief. |

## 5. Reference ports read

- marathon-nx: `C:\devkitPro\MarathonRecomp-NX\docs\SWITCH-OPTIMISATIONS.md` (catalogue 2.1-2.4),
  `out\reports\codegen_proposals_for_banjo.md` (all six already decided in Banjo).
- UnleashedRecomp-NX: `C:\devkitPro\UnleashedRecomp-NX\docs\SWITCH-PERFORMANCE.md` ("What changed" table).
- sa2-nx: `D:\sonic adventure 2\sa2-nx\docs\cpu-rounds.md` (the SDK list of fifteen and the three rounds).
- sasrt-nx: `D:\sonic transformed\sasrt-nx\docs\enhancements.md` (CPU rounds, `index_no_fingerprint`, "hot loops
  versioned", vector registers in calling functions).
- peggle2-nx: `D:\peggle 2\peggle2-nx\out\reports\codegen_proposals_for_banjo.md` (its three are done in Banjo).
- futari-nx: found at `D:\mushihemesama\futari-nx` (also `-crash`, `-installer`, `-ref`); `docs\futari-notes.md` and
  `docs\research\cpu-optimisations-for-rexglue-ports.md` (native LZSS decompressor: precedent for proposal 16).

## 6. For the next console profile

- Keep the ELF of the exact NRO next to the logs (`renut.elf` in the build folder): this report had to align a newer
  ELF by return addresses, and two changed renderer files could only be attributed approximately.
- Run once with `banjo_renut_record_worker = true` to separate recording from replay on the render thread.
- Before and after proposal 1: the render thread's `PmullHash` share and the exact-check counters
  (`CountExactCheck`), plus the verify-mode count of "pages clean but hash differs" from a PC run (must be 0).
