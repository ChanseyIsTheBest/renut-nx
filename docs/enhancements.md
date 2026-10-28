# Performance enhancements and their switches

The reference pages: `docs/enhancements-reference.md` (every run-time key: what it does, why it is exact, how it is
checked, what was measured) and `docs/codegen-enhancements.md` (the build-time changes to the generated game code).
This page is the history: the console runs and the rounds of work after each.

Every enhancement can be turned off from `renut.toml`. An enhancement is active only when its own key **and** its
group's master switch are on. The log has one `[enhancements]` line at startup that lists each one and whether it ran
(`src/cpu/banjo_enhancements.cpp`).

| Master switch | Default | Covers |
|---|---|---|
| `banjo_cpu_enhancements` | `true` | host CPU work: guest-code replacements, waits, thread usage |
| `banjo_noncpu_enhancements` | `true` | GPU, driver and storage side: shader compile cost, caches, prewarming |

## Runtime keys

| Key | Group | Default | What it does | Same output? |
|---|---|---|---|---|
| `banjo_wait_blocking_ring` | CPU | `true` | The game's D3D GPU waits (`sub_8264DCD0`, called by six loops: ring space, Swap, fences) sleep until the native ring thread writes the read pointer back or a fence, instead of spinning (on the console three game threads spun at ~55 % each). The first check of each wait never sleeps (Swap tests its condition after it). | Yes: the game reads the same values, only later. |
| `banjo_repack_cached` | CPU | `true` | k_10_11_11 / k_11_11_10 vertex attributes are converted in a cached scratch copy and written to the upload buffer once (the upload buffer has no CPU cache on the Switch; converting in place read every word back from it). | Yes: same bytes. |
| `banjo_ring_registers_in_place` | CPU | `true` | A proven rectangle list edits the two registers its proof needs in place and restores them, instead of copying the whole register file per list. | Yes: same proof (nothing else in it reads those registers). |
| `banjo_direct_state` | CPU | **`false`** (until the console A/B) | The direct renderer, step 1 (from sa2-nx): the state packets each Direct3D draw call writes become a host record the ring thread applies directly, in place of parsing them as PM4 (see "Direct renderer" below). Off by default: identical image, but on PC the ring's non-draw work was 13 % higher with it; `configs/banjo_direct_ab.toml` measures it on the console. | Yes: same register writes at the same place in the stream; every record is checked at the draw and again by the ring (first 4096 / 2048, all of them with `banjo_cpu_verify`). |
| `banjo_no_draw_pairing` | CPU | `true` | Skips the Draw* record pairing inherited from masseffect-nx, which never pairs a draw in Banjo (its device address and shader offsets are Mass Effect's: 0 of 10,176,920 draws paired on PC); the ring identifies every shader from the IM_LOADs by itself. No record per draw on the game threads, no search per draw on the ring. | Yes: same shaders; verify mode still runs the pairing and compares. |
| `banjo_ring_edram_memo` | CPU | `true` | The direct renderer, step 1b (sa2-nx's `sa2_cpu_ring_edram_memo`): a draw whose render-target registers equal the previous draw's reuses that draw's EDRAM preparation, only when the full preparation would do no work (same images, their tiles owned in the current epoch, no deferred stencil pending). | Yes: the first 4096 hits (every hit with `banjo_cpu_verify`) run the full preparation too and compare the images, area, plan and stencil state, and that it did no work. |
| `banjo_ring_constants_dedupe` | CPU | `true` | sa2-nx's constants dedupe: a new shader-constant generation whose bytes equal the block uploaded last keeps that block bound (no upload, no new set-4 offset). In Banjo only ~3 % of re-uploads are identical (per-object constants really change), so the effect is small. | Yes: exact byte compare against a CPU copy of the last upload; verify mode also reads the uploaded block back (39,581 checks, 0 differences). |
| `banjo_note_shader_fast` | CPU | `true` | sa2-nx's `sa2_cpu_note_shader_fast`: the draw threads' `NoteShaderObject` (twice per draw) remembers finished shader objects in one shared table of 4096 relaxed atomics instead of a 512-slot `thread_local` table (on the Switch every `thread_local` access is a call to `__aarch64_read_tp`; each miss took the shared mutex and a hash-set insert: 4-5 % of the PC draw threads). | Yes: "finished" only ever becomes true; a hit returns where the locked path would. |
| `banjo_ring_target_memo` | CPU | `true` | sa2-nx's `sa2_cpu_ring_target_memo`: `GetTarget` / `GetDepth` (every slot of every draw) answered from a 4-slot memo of their inputs. | Yes: the maps never drop an entry, so the same inputs give the same image; verify mode runs the lookup on every hit (608,613 checks, 0 differences). |
| `banjo_ring_wait_vblank` | CPU | `true` | The ring's WAIT_REG_MEM loop polled every 50 µs; its one long wait (~13 ms a frame on PC: memory `1FB04006`, the frame pacing word) cost ~260 kernel sleeps and wake-ups a frame on the Switch. After 16 polls it now sleeps until the next vblank interrupt or submission wakes it, or 250 µs. PC: 24 million polls per 10 s → ~6,000. | Yes: timing only (the same word is read until it matches). |
| `banjo_ring_vertex_containment` | CPU | `true` | Sonic Transformed's `sasrt_cpu_vertex_containment`: a draw's vertex window inside a larger copy of the same vertex buffer already in this upload buffer reuses it at an offset instead of copying again (the same-frame dedupe only reused identical ranges). PC, `banjo_diag_vertex_union`: 74 % of the vertex bytes the draws use lie inside an earlier copy of the same buffer within the frame. Once the reuse was bounded by the guest synchronization points (`banjo_ring_sync_fences`, below) and kept out of Direct3D's command-buffer pages, the saving is ~10-30 % of the bytes (it was 55-60 % while those were not respected). | Yes: the dedupe's own rules (large ranges, forgotten at every guest synchronization and every upload buffer); the first 4096 reuses (every reuse with `banjo_cpu_verify`) are compared word by word with guest memory: 17,878 checks, 0 differences. |
| `banjo_ring_index_epoch` | CPU | `true` | The 16-bit index cache hashed every draw's index range up to 16 KB (XXH3, ~36 % of the ring's hashing on PC) to find the same indices converted earlier in the frame; above 16 KB it already matched by address. Now every size matches by address within a synchronization epoch, the rule the dedupe's large ranges and the containment use. | Yes: the cache is flushed at every guest synchronization, and data an earlier draw read cannot be rewritten by the game before it synchronizes with the GPU. The first 4096 hits (every hit with `banjo_cpu_verify`) hash and compare: 382,754 checks, 0 differences. |
| `masseffect_native_upload_memory = 1` | CPU | `0` in code, `1` in `renut.toml` | The upload buffer (every draw's vertices and indices) in CPU-cached memory, published with `vkFlushMappedMemoryRanges` before each submit (sa2-nx round 3: the console wrote 5,251 MB/s there against 2,506 in the uncached type). Switch only: on the PC's discrete GPU the cached type is system memory and the ring got 2.5-3x slower per draw (10-16 µs against 4.2-5.2), so PC runs keep 0. `configs/banjo_upload_uncached.toml` is the console A/B. | Yes: the same bytes reach the GPU. |
| `masseffect_native_fingerprint_mode = 2` | CPU | `1` from the enhancement table, `2` in `renut.toml` | The A57's hardware CRC32 pair instead of XXH3 for the vertex/index/texture fingerprints (sa2-nx round 6; `masseffect_crc_fingerprint.h`). The PC build has no CRC instructions and keeps XXH3. | Yes: the fingerprints only decide same-frame reuse; 2^-64 collision odds. |
| `banjo_ring_memexport_bounds` | CPU | `true` | `MemexportRedirect` (every vertex stream of every draw) walked every memexport mirror whenever a stream fell inside the mirrors' overall bounds (2.3 % of the PC ring's work). A sorted index of the live mirrors and their largest size answers "does any mirror overlap this stream?" from the few that can reach it; only then the original walk runs. | Yes: a mirror's base and size never change after it is made; verify mode runs the walk whenever the index says "none" (0 differences on the route with the water's memexport streams). |
| `banjo_native_reverb` | CPU | `true` | Native replacement of XAudio's reverb (`princeton_digital::stereo_room_t<float>::process`, 0x826683D0), ~25 % of a core on the console. `banjo_native_reverb_verify = true` runs both and compares. | Yes: bit-identical (checked by the verify mode). |
| `banjo_mesa_disk_cache_off` | non-CPU | `true` | Switch: `MESA_SHADER_CACHE_DISABLE=true` before the Vulkan device exists. The renderer's own pipeline cache file already persists everything; Mesa's SD-card writer held the cache lock the ring thread waited on mid-compile. | Yes. |
| `masseffect_native_pipelines_prewarm` | non-CPU | `true` | Compiles the pipelines of the list in `cache/masseffect_native_pipelines.bin` on lowest-priority threads at startup, so the ring finds them in the Vulkan cache. | Yes: the prewarmed pipelines are destroyed unused. |
| `banjo_prewarm_threads` | (parameter) | `1` | Threads for the prewarm above (1-3). More threads mostly queue on the console's global malloc lock. | Yes. |
| `banjo_prewarm_threads_rebuild` | (parameter) | `1` | Prewarm threads when the session starts without the file's pipeline cache, so the list compiles for real (1-3). It was 2 for package 20c/20d: on the console that took a core from the game (its threads have the same priority, 0x3B). | Yes. |
| `banjo_ring_core` | (parameter) | `1` | Pins the ring thread (the frame-rate limit) to core 1: it floated over cores 0-2 (99.3 % of one thread spread so no core showed it) and lost its cache on every move. -1 = not pinned. In-run A/B target 41. | Yes: only where the thread runs. |
| `banjo_ring_decoder` (+ `banjo_ring_decoder_core`, 2) | CPU | `true` | Step 3 of `docs/d3d-full-replacement.md`: the ring split. A decoder thread does the PM4 work (packets, register writes, shader identification, the tile passes' bookkeeping, WAIT_REG_MEM) and hands the ring thread, now the executor, the draws in stream order with the register pages written since the item before; fences, memory writes, the tiling extents, interrupts and the read pointer stay the executor's, in stream order; copies, the Swap and the reports run on the executor while the decoder waits. The decoding was about 12 % of the console ring's samples. In-run A/B target 43 (off half: the decoder waits for every draw, one thread's order of work). | Yes: the executor's register file equals the decoder's at every item (pages, and every 1024th draw, 16th in verify mode, the whole file compared: a difference makes every later draw carry the whole file); the game learns of GPU progress only from the executor. |
| `banjo_ring_vs_hash_given` | CPU | `true` | Step 7 of `docs/d3d-full-replacement.md`: the vertex input cache's key takes the XXH3 of the vertex program the ring computed when it loaded the program, instead of hashing the whole program (thousands of words) again on every draw. In-run A/B target 44. | Yes: the same function over the same words (the ring's hash is of the stage's program, which the draw's span is); the first 4096 keys (all with `banjo_cpu_verify`) hash again and compare. |
| `banjo_d3d_records` | CPU | `true` | Step 4 of `docs/d3d-full-replacement.md`: every Direct3D call that writes into a command buffer (the draw functions, Swap, Resolve, Clear, the tile functions, fences: 60 generated hooks, `tools/d3d/d3d_record_hooks.py`, and the hand-written ones) has all its packets made into host records the decoder applies, one marker NOP each; only indirect buffers and WAIT_REG_MEM stay PM4 (step 6b carries them too). The decoder parses no PM4 for them. In-run A/B target 45. | Yes: every record re-encodes to the packets it replaces (`SamePiece`: the first 4096, all in verify mode) and is decoded again from the command buffer when the decoder reaches it (the first 4096, all in verify mode, tile replays included: that catches a patch Direct3D makes after the call); the tile masks are read by reference. A difference stops new records. |
| `banjo_d3d_records_native` | CPU | `true` (with `banjo_d3d_records`) | Step 5: the native Direct3D draw path writes its records' operations itself as it writes its packets (register runs from the shadows, the draw packets and the tile wrapper); only what Direct3D's own helpers write in between is decoded. No decoding of the native packets on the drawing threads. In-run A/B target 46. | Yes: the operations are compared with the packets the same call wrote (the first 4096, all in verify mode); a difference makes the hook decode from then on. |
| `banjo_d3d_host_submit` | CPU | `true` | Step 6: Direct3D's kick hands the segments it submits to the decoder through a host queue (worked out from the kick's arguments before the original runs, queued when the game stores the predicted write pointer): the decoder no longer parses the primary ring. In-run A/B target 47. | Yes: the queued segment is the packet the kick writes; the first 4096 (all in verify mode) are compared with the ring's words, a difference parses the ring from then on. |
| `banjo_d3d_records_barriers` | CPU | `true` (with `banjo_d3d_records`) | Step 6b: records also carry the indirect buffers and WAIT_REG_MEM, so no PM4 is left in Direct3D's segments. | Yes: the decoder runs the same packets from the record. |
| `banjo_d3d_records_static` | CPU | `true` (with `banjo_d3d_records`) | Step 6b: a buffer without records that runs a second time with the same words (Direct3D's fixed buffers 1FB00000 and 1FB01980: per-tile set-up, run before every recorded buffer) is applied from operations decoded once; its words are compared with the kept copy on every run. | Yes: exact compare of the words (not a fingerprint) on every run; a rewritten buffer is decoded again. |
| `banjo_records_zero_ahead` | CPU | `true` (with `banjo_d3d_records`) | ARM audit of steps 4-6 (from sa2-nx's `sa2_api_ring_zero_ahead`): the ring of records' free space just past its head becomes cache lines of zeros (DC ZVA on the Switch), so the records' stores do not wait for a DRAM read of each line (the 8 MB ring is four times the A57's L2; sa2-nx's console profile had most of its publish's samples there). A memset of the same blocks on the PC. | Yes: only free space is touched (past the published head, below the consumer's tail plus the ring's size), checked block by block in verify mode. |
| `banjo_deferred_packets` | CPU | `true` (with deferred recording) | Deferred recording queues the draw path's per-draw commands (pipeline, descriptor sets, push constants, dynamic state, vertex and index buffers, the draw) as packed records, many draws per queue slot, instead of one slot per Vulkan call. PC: 665,000 queued calls per 10 s -> 96,000 calls plus 668,000 records in 7,525 slots; the ring's recording part 3.06 us a draw against 3.17 with the layer off. | Yes: the same commands in the same order (an open chunk is closed before anything else is queued); PC A/B picture check clean. |
| `banjo_deferred_core` | (parameter) | `2` | Pins deferred recording's worker to core 2 (it was created on core 0 with the UI thread). -1 = not pinned. | Yes: only where the thread runs. |
| `banjo_background_compile_core` | (parameter) | `2` | Pins the background compile threads (prewarm, background specialisations, shader statistics copies, shader preload) to one core: at most one core of three away from the game. -1 = not pinned. | Yes: only where the threads run. |
| `banjo_gpu_bool_spec_direct` | non-CPU | `true` | A light-specialised pipeline that is not cached and has no compiled generic twin is created at once (one compile of the smaller variant) instead of the generic one on the ring plus the specialised one in the background; the prewarm skips generic twins. Halved compile work on the PC with an empty cache. | Yes: the same pixels, only which pipeline is compiled changes. |
| `banjo_gpu_spec_per_stage` | non-CPU | `true` | Each pipeline stage gets only the specialization constants its SPIR-V reads, so NVK's per-stage cache reuses a vertex shader across light combinations and alpha functions (about 56 % fewer vertex compiles on the PC's list). | Yes: every constant a module reads has the same value. |
| `banjo_pipeline_cache_max_mb` | (parameter) | `128` | The pipeline cache in `cache/masseffect_native_pipelines.bin` is loaded only if it was built with the shader package in use and is at most this many MB; otherwise it starts empty and the prewarm list (always kept) rebuilds it. Stops old packages' entries piling up (the console's file had reached 258 MB). | Yes: it only decides which compiled pipelines are reused. |
| `masseffect_native_flat_identity`, `_fast_pair`, `_raw_microcode`, `_pm4_fast`, `_compare_fast`, `_fast_copy`, `_fingerprint_mode = 1`, `_dedupe_max_fingerprint = 16384`, `_indices_cache`, `masseffect_notify_no_lock` | CPU | off in code, on in `renut.toml` | The ring thread's fast paths from masseffect-nx's measured best profile. | Yes: each compares itself with the regular path (first `masseffect_native_verify_n` uses, or every use with `banjo_cpu_verify`) and logs DIFFERENCE/MISMATCH and falls back if they ever disagree. |
| `masseffect_native_deferred_recording` | CPU | **off** (explicit only; `false` in `renut.toml`) | The ring queues the Vulkan commands and a worker thread records them. **Broken for Banjo**: the game reads rendered images back to the CPU hundreds of times per 10 s and each read-back drains the queue and waits for the worker. On PC, with only the base switch, the ring waited 9.6 s of every 10 s and the game never left the "LOADING" screen; with masseffect-nx's companion settings it passed but still waited 1.7 s per 10 s. | Same commands, but not usable here. |
| `masseffect_native_diag_reuse = false` | CPU | `true` in code, `false` in `renut.toml` | Turns off a measurement (content fingerprints of every new texture, on the ring). | Yes: measurement only. |
| `log_file_writer` | CPU | `true` (SDK cvar; `false` set explicitly in the cpu-off configs, it is read before the master switches) | From futari-nx (2026-10-07): the file log is written by a thread of its own (`sdk/src/core/logging.cpp`, `WriterFileSink`). The logging thread only formats the line into memory; the "Log writer" thread (priority 0x3B on the Switch) writes the batch with one `fwrite` (64 KB buffer) once a second, sooner at 64 KB or on a warning. Futari's console run: every frame over 100 ms (one of 1,022 ms) came while the ring thread wrote its 10 s report, inside `fsdev_write` (newlib's 1 KB FILE buffer made each long line an SD write; 50-150 ms each with music streaming from the same card). Past `log_file_writer_buffer_kb` (4 MB) lines are dropped and counted, never waited for; `FlushLogging` (crashes, exit) still writes everything; a Switch fatal error appends the unwritten tail to `rex_crash.log`. PC check: 3,005 lines, all in order. A/B: `configs/banjo_logwriter_off.toml`. | Yes: the same lines, written later (up to 1 s; a killed process loses at most that). |
| `banjo_gpu_boost` | non-CPU | `true` | From sa2-nx's `sa2_gpu_boost`: in handheld mode asks for the official 0x92220008 profile (GPU 460.8 MHz, memory 1331.2 MHz; UnleashedRecomp-NX, MarathonRecomp-NX and masseffect-nx use the same) through the SDK's `switch_apm.cpp`, which checks the real clocks for 1.5 s and reverts if the memory clock moved; docked, nothing is requested (`renut_app.h`, `OnPreSetup`; `rex_stderr.log` says what it did). Risk: more power draw handheld. | Yes: a clock, not a setting. |
| `banjo_vertices_base_zero_r0` | CPU | `true` | From sa2-nx's `sa2_cpu_vertices_base_zero_r0`: draws whose vertex shader reads r0 (all of Banjo's) also take the base-zero vertex binding (the upload buffer bound once, the draw shifted by firstVertex/vertexOffset), with g_VertexIndexBase = vmin - first_vertex so r0 (XenosRecomp: `float(iVertexId + g_VertexIndexBase)`, iVertexId including the base vertex) is unchanged. The decision moved ahead of the shared constant block. PC: vkCmdBindVertexBuffers per draw 0.99 -> 0.05 (NVK: 5 dwords + an MME macro each, ~1 us on the A57). | Yes: the same r0 for every vertex; the zero-base guard re-derives the arithmetic. |
| `banjo_ring_bound_plan_memo` | CPU | `true` | From sa2-nx: `PlanEdramBoundRanges` (a pure function of its request) is reused when the request equals the previous draw's (PC: ~80,000 reuses per 10 s). Verify mode recomputes and compares. | Yes (verify: 0 differences). |
| `banjo_ring_estimator_reason` | CPU | `true` | From sa2-nx: the draw-extent estimator keeps its reject reason as a pointer to the literal instead of a std::string (a heap allocation per call). | Yes: diagnostics text only. |
| `banjo_gpu_resolve_draw` | non-CPU | `true` | From sa2-nx's `sa2_gpu_resolve_draw`: a same-format RGBA8 color resolve is one fragment pass on the 3D engine (`sa2_resolve_copy.frag`, texelFetch of source + delta) instead of vkCmdCopyImage, which NVK records on the copy engine with a wait for idle at each engine switch (Banjo: ~30 resolves a frame). PC: ~590 resolves per report drawn, 0 declined; GPU "copies" 5.7 -> 4.4 ms per Swap. | Yes: UNORM8 round-trips exactly through float (as the EDRAM color conversions). |
| `banjo_gpu_edram4_clear_color0` | non-CPU | `true` | From sa2-nx's `sa2_native_edram4_clear_color0` (gpu-optimisation-candidates #1): Direct3D's depth + color 0 Clear drawn through a 4x alias is cleared in the 1x owners of both tiles (color from the clear's interpolator 0, `ProvePixelColor0IsR0`) instead of drawn and transferred 4x -> 1x. PC: 43 such clears per report redirected (16,128 tiles); 58 still declined (owner not RGBA8). | Yes: the transfer copies words verbatim, so the owner gets the bytes it would have received; values near an 8-bit rounding tie are declined. |
| `banjo_gpu_push_once` | non-CPU | `true` | From sa2-nx: the three push-constant addresses (the set-4 shaders never read them; Banjo's library has MASSEFFECT_UBO true at compile time) are pushed once per recorded state, not per draw whose constant offsets moved (NVK: a 6-word root-table upload per draw less). PC: ~55,000 pushes saved per report. | Yes: unread values; a draw of a pipeline without set-4 constants would be a DIFFERENCE and turn it off. |
| `banjo_gpu_pass_narrow_end` | non-CPU | `true` | From sa2-nx's `sa2_native_pass_narrow_end` (#10): a render pass's end dependency names its consumers (shader, transfer and attachment access) instead of MEMORY_READ | MEMORY_WRITE, so NVK does not invalidate its constant, QMD and macro caches at every pass end. Banjo's memexport streams are shader storage writes, not covered by this dependency either way. | Yes: the same ordering for everything that reads attachments. |
| `banjo_gpu_blend_skip` | non-CPU | `true` (needs the new shader package) | From sa2-nx (round 1): pixel shaders discard a pixel whose blend would leave the RGBA8 target unchanged (SPEC_CONSTANT_BLEND_SKIP_ALPHA/_ZERO; one color target, no alpha to coverage, no depth or stencil write, op ADD, destination factor 1 once the source is 0). 683 pixel shaders have the variant. | Yes: a NaN output is never discarded; occlusion queries are answered on the CPU. |
| `banjo_gpu_structured_cf` | non-CPU | `true` (shader package) | From sa2-nx (round 2): shaders whose forward-only control flow the translator proved equivalent (every jump outcome) run it as nested if/else instead of an `if (skipTo <= pc)` per CF instruction. Only 3 of Banjo's shaders fit. | Yes: proven at translation time. |
| `banjo_gpu_early_alpha` | non-CPU | `true` (shader package) | An alpha-tested pixel shader runs a copy of its alpha computation right after the last instruction the alpha needs (tools/early_alpha.py at library build time, SPEC_CONSTANT_EARLY_ALPHA), and a 2x2 quad whose four pixels all fail the test there stops before the rest of the shader. A/B target 26. See "Early alpha test" below. | Yes: the copy is the same operations on the same values (checked against the original by the tool on random inputs); a pixel fails early only when the test fails for every alpha within (|alpha| + |ref|) * 2^-12 of it; whole quads only, so no shaded pixel loses a derivative neighbour. |
| `banjo_ring_pmull_fingerprint` | CPU | `true` (ARM only) | Where `masseffect_native_fingerprint_mode = 2` hashes with the CRC32 pair (vertex windows, index ranges, sampled blocks) and for the texture recheck's whole-content hash, a 64-bit CRC folded with PMULL carry-less multiplies (`banjo_pmull_fingerprint.h`): half the instructions per byte on the A57. See "PMULL fingerprint" below. | Same guarantees as the CRC32 pair (every change under 64 bits seen, otherwise 2^-64); checked against a bit-by-bit reference on the console while it runs (a difference switches it off for the session) and on the PC through simde (`sdk/tests/pmull_fingerprint_test.cpp`). |
| `banjo_gpu_texcoords_unswapped` | non-CPU | `true` (shader package) | From sa2-nx: vertex shaders skip the swapped-texcoord test (g_SwappedTexcoords is always 0 here). | Yes (verify checks the word is 0). |
| `banjo_gpu_no_texture_signs` | non-CPU | `true` (shader package) | From sa2-nx: when no texture of the draw (pixel, vertex shader and s16/s17 slots) has a signed, biased or gamma component, the shaders skip the per-fetch sign test (~61 % of PC draws). | Yes: the skipped code is the early return the zero signs take anyway. |
| `banjo_gpu_identity_remap` | non-CPU | `true` (shader package) | From sa2-nx: a pipeline whose vertex inputs all keep their component order skips the per-input remap test. | Yes (verify checks every code is 0xFFF). |
| `banjo_gpu_index_base_held` | non-CPU | `true` (shader package) | From sa2-nx: vertex shaders the translator proved never read the vertex index (301 of Banjo's) run with r0 starting at 0 and g_VertexIndexBase held at 0, so consecutive draws keep the same shared constant block (not rectangle lists or tessellated draws). | Yes: the index is provably unread. |
| `banjo_gpu_texture_signs_per_slot` | non-CPU | `true` (shader package; off in 20e only) | A draw with signed, biased or gamma textures keeps the per-fetch sign / gamma code only in the fetches of those fetch constants (specialization constant 61, a bit per fetch constant, kept in the pipeline key). PS536: 3,325 -> 2,075 SPIR-V instructions offline. A/B target 27. See docs/shader.md. | Yes: a fetch constant whose texture words all have packed signs 0 takes that path anyway (verify checks every texture word against the bits). |
| `banjo_gpu_branchless_select` | non-CPU | `true` (shader package) | The translator's predicate updates and kill tests (and early alpha's copies) are select() / and() instead of HLSL 2021's short-circuit ?: and &&, which DXC writes as branches that NAK keeps. PS536 54 -> 37 branches. A/B target 30 (bit 3 = the old forms). | Yes: no side effects in the operands. |
| `banjo_gpu_pred_select` | non-CPU | `true` (shader package) | A vertex shader's predicated blocks (`if (p0)`) that hold only register arithmetic, static constants and bone fetches run for every vertex, their results kept with select() where the predicate holds: NVK flattens nothing with a load in it, so each bone was a branch and its loads waited for the previous bone. Skinning shaders 4 -> 1 branches. A/B target 37 (bit 2 = the old form). | Yes: no side effects, the fetches are clamped. |
| `banjo_gpu_pred_select_ps` | non-CPU | `true` (shader package) | The same for a pixel shader's blocks of up to 12 statements (no texture fetch, kill, output or derivative in them). Heavy material shaders 37 -> 19-25 branches. A/B target 38 (bit 4). | Yes: as above; early alpha reads the construct. |
| `banjo_gpu_clamp_cbuf` | non-CPU | `true` (shader package) | The Xenos RCP / RSQ / LOG clamps take -FLT_MAX / FLT_MAX from two words of the shared block (a free constant-buffer operand of NAK's min / max) instead of two 32-bit immediates NAK moves into registers before every use: 12-27 moves in each heavy pixel shader, 6,593 clamps in the pixel shaders. A/B target 49. | Yes: the same two values, the same min / max. |
| `banjo_gpu_sign_mask_skip` | non-CPU | `true` | A texture slot's word loses its packed sign 1 (signed, fetched as sampled like 0), so a slot without an effective sign has a top byte of 0 and the shaders skip `& 0x00FFFFFF` there (one instruction per texture handle). A/B target 50. | Yes: sign 1 and 0 take the same path in the shaders; the mask stays where an effective sign can be. |
| `banjo_gpu_vs_outputs_unwritten` | non-CPU | `true` (shader package) | A vertex output the pipeline's pixel shader does not read is not written at all (it was written as 0), so the driver drops its store and its attribute: the 4-bone skinning shaders stored 16 zero attributes a vertex. A/B target 51. | Yes: the pixel shader never reads them (constant 60). |
| `banjo_gpu_bone_padding` | non-CPU | `true` | Every indexed vertex fetch buffer (bone palettes, the water grid) is uploaded with zeros after it, so an indexed fetch is a clamp and a load without a compare and a select per word (48 selects a vertex in the skinning shaders). Buffers that are not these uploads (memory exports) keep the old form for their draw. A/B target 52. | Yes: past the end still reads 0. |
| `banjo_gpu_robustness_off` | non-CPU | `true` | The game's pipelines are created with buffer and image robustness disabled (`VK_EXT_pipeline_robustness`, which the SDK now enables), so NVK drops its two descriptor index clamps from every texture handle. A/B target 53. | Yes while every descriptor index is in range, which the renderer guarantees. |
| `banjo_gpu_edram_checks_cpu` | non-CPU | `true` | The multisample EDRAM import shaders' image checks (sample counts and sizes: four texture queries in every pixel of every pass of the 9-pass stencil import) are made once on the CPU; the pipelines are created without them. A/B target 54. | Yes: an import whose check fails is refused, which the shaders' discard of every pixel amounted to. |
| `banjo_gpu_tess_grid` | non-CPU | `true` (shader package) | The water's tessellated patches are drawn indexed, one vertex invocation per grid point instead of one per triangle corner: 25,344 -> 5,049 invocations a draw (each point was computed up to six times). A/B target 39. | Yes: each point's domain from the same (a, b), same triangles in the same order. |
| `banjo_gpu_vfetch_block` | non-CPU | `true` (shader package) | Bone palette fetches load each word at its index clamped into the buffer and select 0 outside it, with no branch (before: a test and a branch around each of 48 loads per vertex). Skinning vertex shaders ~2,400 -> ~1,200 SPIR-V instructions, 100 -> 4 branches. A/B target 29 (bit 7 = the old form). | Yes: the same word or 0 for every word; an empty buffer gets a readable address. |
| `masseffect_native_vs_pruned_outputs` | non-CPU | `true` (shader package) | Vertex shaders set the outputs the pixel shader does not read to 0 at their exit (specialization constant 60, passed by the renderer all along but never declared by Banjo's translator until package 20), so the math that fed them goes. | Yes: the pixel shader never reads them. |
| `banjo_gpu_nvk_draw_contract` | non-CPU | `true` | From sa2-nx (gpu-optimisation-candidates #9), 2026-10-13: NVK's per-draw contract is also looked up under the name the linked Mesa tree exports. The app declared `nvk_switch_draw` (weak); the Mesa of `mesa-switch-main-nfsmw-source` exports the same structure as `nvk_switch_dibujo` (the ELF of 2026-10-12: `D nvk_switch_dibujo`, no `nvk_switch_draw`), so the reference was null and the four per-draw paths the `masseffect_native_nvk_*` keys ask for (draw emission in one go, only the cbuf slots and dynamic state groups that can be dirty, prefetch hints) were **never requested in any Banjo Switch build**; the log said `this NVK lacks the nvk_switch_draw contract`. No driver change: the paths are in the driver already and sa2-nx ships with them. Switch only; on the console look for `C6 NVK per draw: improvements requested` and the `C6 NVK parts` report. | Yes: each path checks itself inside NVK and its guard switches it off on a difference (logged as MISMATCH, counted as a verify difference). |
| `banjo_native_partial_resolve_inside` | non-CPU (correctness) | `true` | From sa2-nx's 1.0.1, with Banjo's rule: see "sa2-nx's CPU rounds" below. | A fix: without it a texture can be drawn into a freed neighbour. |
| `banjo_cpu_guest_cas_no_barrier` | CPU | `true` | sa2-nx's CPU rounds: compare-and-swap without the legacy builtin's trailing `dmb ish` (559 sites in the game code and the SDK's lock words). false puts the barrier back at run time. | Yes: PowerPC's lwarx / stwcx. order nothing; the game's own barriers are translated separately. |
| `kernel_critsec_pcr_owner` | CPU (SDK key) | `true` | sa2-nx's CPU rounds: a critical section call takes its owner from the guest's processor block instead of a thread lookup (24,000 enters a second on PC). | Yes: the PC build compares both on every call (18 million in a verify session, 0 wrong). |
| `thread_signal_skip_idle` | CPU (SDK key, Switch) | `true` | sa2-nx's CPU rounds (from sasrt-nx): no condition-variable notify (a system call on Horizon) for an event or semaphore nobody is blocked on. | Yes: the waiter count is read under the same mutex as the state change (sdk/tests/signal_skip_idle_test.cpp). |
| `banjo_ring_vertex_persistent` | CPU | `true` | A vertex window whose bytes were the same in three frames keeps its converted copy in a persistent buffer (64 MB, `banjo_ring_vertex_persistent_mb`); a later draw of it binds that buffer and copies nothing. The guest's bytes are fingerprinted whole once per synchronization interval instead of copied. See "Static vertex buffers kept across frames". | Yes: a block is bound only while the fingerprint of all the guest bytes is the one it was made from; the first 4096 uses and one in 4096 after are compared byte by byte (every use with `banjo_cpu_verify`). A difference turns it off for the session. |
| `banjo_ring_vertex_persistent_submitted` | CPU | `true` | Part of the above: a block whose bytes were read stays proven past a synchronization point for the draws the game had already submitted at that moment. Applies in the intro (45,000 to 64,000 uses per 10 s there); not in gameplay, where the game waits for the ring after every piece of the frame. | Yes (the vertices of a submitted draw are final and stay untouched until the GPU has run it); every such use fingerprinted again with `banjo_diag_vertex_persistent_check`. |
| `banjo_ring_shader_load_memo` | CPU | `true` | Direct3D loads a shader program (IM_LOAD) before most draws: about a thousand times a frame with another program than the stage's last. Each such load copied the words three times and hashed them to find the entry identified for that content. A memo by load address now keeps the words, their hash, the entry and the identity generation; a load from that address with the same raw words (one `memcmp`) swaps them into the stage. | Yes: the same state the content memo's hit leaves (checked against it on the first 4096 uses, all in verify mode). |
| `banjo_ring_vertex_epoch` | CPU | `true` | Within a synchronization interval a vertex window of any size is matched to its earlier copy by address, as the windows above 16 KB, their containment and the index cache already were; the windows up to 16 KB were fingerprinted (every 8th block) on every draw. Containment for every size too. Ranges in Direct3D's command buffer pages keep their fingerprint. | Yes: the rule `banjo_ring_sync_fences` makes exact; the first 4096 small reuses, all in verify mode, compared with guest memory. |
| `banjo_ring_constants_adaptive` | CPU | `true` | `banjo_ring_constants_dedupe` stops comparing (and copying its shadow) for a shader stage whose last 1024 compares found fewer than 16 equal blocks, for that stage's next 16384 uploads, then looks again. On the console the compare found 416 equal blocks in 51,649 vertex shader uploads and 0 in 75,221 pixel shader uploads. | Yes: it only stops trying. |
| `banjo_gpu_zcull_greater` | non-CPU (Switch) | **`false`** (console trial) | Asks the driver for hierarchical depth culling in the GREATER direction (its own option, `NVK_ZCULL=greater`). Banjo's scene is drawn with reversed depth (GREATER_EQUAL, cleared to 0), so the driver's default direction ("less") gives the scene no culling. | Should be: ZCULL only rejects pixels the depth test would reject. Not testable on the PC, hence off until a console run with `configs/banjo_zcull_greater.toml`. |
| `banjo_ring_identity_once` | CPU | `true` | A draw asked up to four times whether its vertex shader entry matches the program that is loaded (`MatchesVertexShaderIdentity`, 2.3 % of the ring thread on the console). It is asked once; when it holds, the two blocks that could only change the entry when it does not are skipped, and the answer serves the rest of the draw. Otherwise everything runs as before. |
| `banjo_ring_sampler_fast` | CPU | `true` | A hit of the per-register sampler cache compares the three fetch-constant words in line and writes 1/size from the entry instead of dividing on every draw. Same words in the shared constants. |
| `banjo_ring_vertex_proven_first` | CPU | `true` | A vertex window already proven in the persistent arena for this synchronization interval is bound from it before the upload buffer's two reuse tables are searched (lookups that cannot find it: its copy is not in the upload buffer). Same binding. |
| `banjo_gpu_dead_raster` | non-CPU | `true` | Banjo's particle simulation runs as point-list draws whose vertex shader writes memory (memexport); what they rasterize into the render target bound at that moment is never used: the next draw that touches a target overwrites those tiles whole. Drawn as they are, they pulled 832 EDRAM tiles a frame into the bound views first. A draw of such a site (shaders, surface, formats, depth control, mask) that has been overwritten whole four times in a row runs with its color mask 0 and depth test/write off: the memory writes stay, no tile moves. Checked on every use: the draw that follows must overwrite every tile the group could have written (all 32 bits for color, depth bits for depth), otherwise it is a `DIFFERENCE` and the key is off for the session. A/B target 14. |
| `banjo_gpu_rect_split` | non-CPU | `true` | Direct3D's Clear of an area that does not end on tile boundaries clears the whole tiles with a resolve and draws the rest as a rectangle list of two rectangles, an L along the right and bottom edges. That draw had one EDRAM area, the box around both rectangles, which is the whole cleared area: 42 tiles converted depth to color every frame, of which the draw touches 16 and the resolve clear overwrites the other 26 right after. An auto-indexed rectangle list of 2 to 8 rectangles is now drawn as one draw per rectangle (three vertices at that rectangle's index offset; the same pixels in the same order). A/B target 15. |
| `banjo_hand_memcpy` | CPU | `true` | Hand-written native of the game's C runtime memcpy (sa2-nx's): overlapping 16/8/4/2-byte moves up to 128 bytes, the host's memcpy above. In front of the generated copy of the function in every hook that contains it (its own hook `banjo_cpu_native_memcpy` and two others). Declines a forward copy that would smear, ranges that wrap, a destination over its own save slot. |
| `banjo_hand_memset` | CPU | `true` | The same for memset (overlapping stores up to 64 bytes, the host's memset above; hook `banjo_cpu_native_memset`). |
| `banjo_hand_constants` | CPU | `true` | Direct3D's two shader float-constant writers (hooks `banjo_cpu_native_constants_vs` / `_ps`, and inside three other hooks' copies): a plain copy of the vectors and the dirty-mask update instead of emulated unaligned vector loads. From sa2-nx. |
| `banjo_hand_state_flush` | CPU | `true` | The two writers of Direct3D's state flush (dirty register words and dirty float-constant blocks into PM4 packets in the command buffer; hooks `banjo_cpu_native_flush_registers` / `_constants`): the runs copied in guest byte order. Declines when a run might not fit the buffer (the original then runs, with its call for space). |
| `banjo_diag_gpu_groups` | measurement | `0` | `1`: the "GPU time by pass" report splits every pass by the state of its draws (label "VS" = 60000 + 1 alpha test + 2 blend + 4 depth write + 8 depth test + 16 no color write). `2`: also by pixel shader ("PS" = its number). One GPU timestamp per change of group; 48 lines instead of 16. For a console run: the PC's split of GPU time is not the console's. |
| `banjo_gpu_proof_tess_leftover` | non-CPU | `true` | The overwrite proof (which lets a clear or a full-screen pass take a target over without importing what EDRAM held) refused every draw while the tessellation register said "enabled". The console only tessellates when the primitive type is also a patch type (the reference rule: `IsMajorModeExplicit && path_select == kTessellationEnable`); Banjo leaves the register set after its tessellated draws, so 16 clears a frame were refused. | Yes: the same rule the emulated renderer and the console use; in-run A/B recording unchanged. |
| `banjo_gpu_proof_triangle` | non-CPU | `true` | The proof also accepts one auto-indexed triangle of three vertices that covers the whole viewport (or scissor, with clipping off): the blur chain and the composite are drawn that way. Exact edge test in double precision; one pixel of margin with clipping off. | Yes: only decides whether old contents are imported before they are overwritten. |
| `banjo_gpu_edram4_color_consumer` | non-CPU | `true` | A colour clear through Direct3D's 4x clear view is made directly in the target the following draws use (learned per clear site, as the depth redirect already did), instead of in the view and then transferred. | Yes: the same pixels get the same value; a clear whose consumer turns out different is transferred as before. |
| `banjo_gpu_edram4_alias_by_shape` | non-CPU | `true` | A depth clear through the 4x view with no learned 1x target finds it by shape (same base, format, twice the pitch). First frames of a scene only. | Yes. |
| `banjo_gpu_edram4_copy_batch` | non-CPU | `true` | Runs of one-tile EDRAM copies between the same two images become one `vkCmdCopyImage` with many regions. | Yes: the same regions. |
| `banjo_gpu_stencil_fetch_3d` | non-CPU | `true` | A late stencil fetch takes the 3D masked passes instead of the copy engine (the console's one steady stencil import a frame: 1.3 ms of `edram_import9` at stock). A/B target 32. | Yes: the masked passes are the original import path. |
| `banjo_gpu_edram4_lean_barriers` | non-CPU | `true` | No full barrier around the EDRAM clear passes (their render pass orders them); the scene depth's resolve gets narrowed barriers. PC: busy GPU -1.6 ms a frame, 10 full flushes a frame left. A/B target 33. | Yes: the same ordering for every access that exists. |
| `banjo_gpu_resolve_clear_merge` | non-CPU | `true` | A resolve's clear records all its rectangles in one pass (depth only for images without a ZCULL plane). A/B target 36. | Yes: the same rectangles and values. |
| `banjo_gpu_depth1x_zcull` | non-CPU | `false` | 1x depth views without TRANSFER_DST so the shadow maps can get ZCULL planes. Off: console-only check, and UnleashedRecomp's GPU loss with depth-clear changes. | Yes by the Vulkan contract; driver-dependent. |
| `banjo_gpu_edram4_clear_merge` | non-CPU | `true` | A redirected depth clear records one full barrier before and one after all its clears, and one LOAD pass per owner image clearing all of its rectangles with one vkCmdClearAttachments, instead of a barrier, a CLEAR pass and a barrier per run of tiles (393 of the level's 427 barriers a frame). A/B target 28. | Yes: the same rectangles with the same values; owners are distinct images or disjoint rectangles; clears that need work first take the old path. |
| `banjo_gpu_tiles_once` | non-CPU | `true` | The scene is drawn once instead of once per predicated tile: see "Removing emulation" below. Goes back to one pass per tile by itself for any frame whose stream it does not recognise, and off for the session if a second-pass draw ever differs from the one made for it (`DIFFERENCE`). | The target pixels are the same. One known difference by design: a draw that samples the resolved scene across the tile boundary sees this frame's rows on the other side instead of the previous frame's (the console shows last frame's there). |
| `banjo_native_mip_range` | non-CPU (correctness) | `true` | A sampler's level range follows the reference rule for a fetch constant without a mip address (levels 0-0), with a null base address (lowest level 1) and clamped to the texture's size. | A fix (matches the emulated renderer). |
| `banjo_native_stacked_baseless` | non-CPU (correctness) | `true` | A stacked (array) texture streamed small-levels-first is built from the first level it has, instead of from level 0 at an address that holds something else. **This was the dark patches on Banjo's fur.** | A fix (matches the emulated renderer). |

**sa2-nx port, 2026-10-07 (the user's "all of SA2's optimisations" build).** Also: `kConversionsEDRAMPerSlot` 256 -> 2048 (gpu-optimisation-candidates #2: 31 fenced "conversion capacity" rollovers per 10 s, each a full CPU<->GPU drain, went to 0), and the shader package rebuilt with sa2-nx's XenosRecomp round 2 (`shaders/XenosRecomp`: structured CF and the vertex-index proof applied as sa2-nx's own patch, blend skip and the spec bits merged into Banjo's memexport/tessellation/select-based shader_common.h; sa2-nx's "interpolators always linked by register" translation change was NOT taken: it changes output for SA2's mixed shader pairs and needs its own check here). The first-boot prewarm list holds only pipelines with the new keys (605; `tools/make_prewarm_list.py --require-spec-bit=5`), so areas past the intro compile their pipelines once on first visit. PC: verify `differences: 0` (every new path engaged), 0 rejected draws, videos of the FMVs, title, menus, intro cutscenes and Spiral Mountain normal. Not ported: SA2's native Direct3D layer (`sa2_api_*`: a transliteration of SA2's 2012 XDK DrawVertices/Clear for one drawing thread without predicated tiling; Banjo needs its own for the 2008 XDK's DrawIndexedVertices, four drawing threads, tiling and replayed command buffers), the direct-state refs/restore/flush steps (Banjo's step 1 is off pending the console A/B), `sa2_gpu_resolve_alias` (built around SA2's duplicate resolves), `sa2_native_linear_fetch_not_resolved` (no cause-109 rejections in Banjo), and micro-items whose Banjo paths are already cheap.

Not switchable (diagnostics bookkeeping only, identical log output): the "bounded draw extent" log dedupe uses a hash
instead of formatting a string per draw, and the EDRAM mode-4 decline reasons and transfer-trace descriptions are
formatted when reported instead of per draw (both from futari-nx's ring profile).

## Verify configuration

`banjo_cpu_verify = true` (`configs/banjo_cpu_verify.toml`) runs every CPU enhancement and checks each against its
original path on **every** use (the self-check counts are raised from 2048 to 2^31-1). The renderer's 10-second report
then has a line

    [verify] checks done: identity memo N, record pairing N, raw microcode N, PM4 runs N, object table N, native game functions N, direct-state records N at the draw and N at the ring, draws without pairing N | differences: D

**D must be 0.** Every DIFFERENCE / MISMATCH report and every native game-function mismatch counts into it
(`src/me/me_verify.h`). It is slower (both paths run), so it is for a test session, not play.

## Direct renderer, step 1 (`banjo_direct_state`)

The orders relayed by the SA2 session: remove the CPU-side emulation from the renderer, PM4 first, with sa2-nx's direct
renderer (`docs/direct-renderer.md` there) as the base. Step 1 is its direct state, adapted to Banjo:

- **What it does.** Right after Direct3D's draw function returns, the draw hook (`EndDrawDirect`,
  `src/me/me_native_system.cpp`) decodes the packets the function wrote before its draw packet (register runs,
  constants, fetch constants, shader loads, tile-mask packets) into a host record, register values already in host
  byte order. One NOP packet then covers those packets in the command buffer (its payload names the record); the draw
  packet and every address Direct3D keeps stay where they were. When the ring reaches the NOP it applies the record
  with the same register functions the packets would have gone through, at the same place in the stream.
- **What is different in Banjo.** The engine records most draws into command buffers of its own on several threads
  (four Direct3D "device" objects on the PC run), and the device's command buffer runs them as indirect buffers, once per
  EDRAM tile (predicated tiling: 2/3 of the indirect-buffer runs are a range already run in the same frame). So the ring
  finds records by number, as often as their marker is reached, keeps them three Swaps, and moves a record whose marker
  is still in its command buffer to a side store until the game writes over the marker. The recording threads take
  turns converting (a short lock). A predicated draw's tile mask is written by Direct3D as a placeholder (0xFFFFFFFF)
  and filled in after the draw call, so those packets are carried by reference (read from the command buffer when the
  record is applied), not copied.
- **Left as PM4:** a draw during which the game submitted to the ring or the command buffer changed segment, packets the
  converter does not carry (none in the PC test), a NOP in the region's first three words, regions under 3 words.
- **Checks:** each record re-encodes to the exact packets Direct3D wrote (at the draw), and the ring re-decodes the
  region the command buffer holds when it reaches the marker, on every tile pass, and compares (first 4096 / 2048
  records; every record with `banjo_cpu_verify`). A difference logs DIFFERENCE, the ring uses the original packets for
  that draw, and no more records are made.
- **A/B in one run:** `banjo_direct_state_ab_s = N` (measurement only, default 0) turns the conversion off and on every
  N seconds and adds `[native] ring thread A/B (...): ON ... | OFF ...`: both halves see the same scenes and the same
  machine load (PC runs share the machine with other projects; sequential runs were not comparable).
- **Report** (every 10 s): `[native] direct state (cumulative): ...` (converted, applied, applied again on tile passes,
  side store, why the others stayed PM4) and `[native] ring thread: X ms of work per Swap (Y us per draw, ...)`, the
  ring thread's own time without its waits for the game.

### Measured on PC (2026-10-07, in-run A/B, 2 s halves, 200 s, title + menus + intro)

| | Ring work per frame | Draw/Copy (Vulkan side) per draw | The rest (PM4, records, identification) per draw |
|---|---|---|---|
| `banjo_direct_state` off / on | 15.5 / 16.5 ms | 6.92 / 7.14 µs | 1.74 / 1.97 µs |
| `banjo_ring_edram_memo` off / on | 14.5 / 15.0 ms | 6.33 / 6.37 µs | 1.72 / 1.73 µs |
| `banjo_ring_constants_dedupe` off / on | 14.1 / 14.2 ms | 6.12 / 6.09 µs | 1.54 / 1.54 µs |

The PM4 the ring parses falls from 18.2 M to 6.0 M packets (type-0 words 77.9 M to 10.9 M), but on PC the record path
costs about what the vectorised PM4 run parser did, plus its bookkeeping. 80 % of the ring's work per draw is the
Vulkan side (Draw/Copy). The PC shares the machine with other projects' tests, so only in-run A/B numbers compare.

## Premade configs

`python tools/make_enhancement_configs.py dist/switch/banjo-nx/renut.toml dist/switch/configs` deletes the old configs
and writes, from the shipped config: `banjo_cpu_on.toml` (as shipped), `banjo_cpu_off.toml`, `banjo_noncpu_off.toml`,
`banjo_all_off.toml`, `banjo_profile.toml`, `banjo_cpu_verify.toml` (profiling off; `banjo_direct_state` on so it
is verified too), and the in-run A/B configs `banjo_direct_ab.toml` / `banjo_memo_ab.toml` (the "ring thread A/B" line;
`banjo_ab_target = 3` measures `banjo_ring_constants_dedupe` the same way). Since 2026-10-13 evening also
`banjo_tiles_once_ab.toml` and `banjo_vertex_persistent_ab.toml` (in-run A/B of `banjo_gpu_tiles_once`, target 9,
and `banjo_ring_vertex_persistent`, target 11) and `banjo_emulation_items_off.toml` (the EDRAM proof keys, tiles once
and the persistent vertices off; the texture fixes stay), and after the second console run
`banjo_shader_load_memo_ab.toml` (target 12), `banjo_zcull_greater.toml` and `banjo_deferred_recording.toml` (the
two console trials: whole-run settings, not in-run A/B). They differ only in the keys their header lists, so two
logs compare directly. Copy one over `sdmc:/switch/banjo-nx/renut.toml`. Note that `banjo_cpu_verify.toml` turns
`banjo_direct_state` on, and `banjo_gpu_tiles_once` stands down while that is on (its own comparison of every
second-pass draw runs in every other config).

## Profiling

On in the shipped `renut.toml` and in every premade config except `banjo_cpu_verify.toml` (user request). Two plain
keys, outside the enhancement master switches:

- `switch_profile_stacks = true`: the SDK sampler (`sdk/src/ui/switch_perf.cpp`) pauses busy threads 1000 times a
  second and writes, every 10 s, every pc with its count to `logs/rex/rex_profile.log`.
  `python tools/switch_hot_functions.py rex_profile.log --map <build>/renut.map` ranks functions by self time.
- `masseffect_gpu_marks_categories = true`: a GPU timestamp at every pass-category change ("GPU per Swap" lines with time
  per category and the idle gap). The image does not change.
- `banjo_profile.toml` also turns on the app's own sampler (`banjo_cpu_profiler`, `logs/rex/banjo_profiler.log`,
  `tools/switch-cpu-profile.py`).

The `[enhancements]` log line states both. Use the ELF and map of the same build as the NRO (`builds/<date>/`).

## Build-time changes (A/B by files, not keys)

| Change | How to A/B |
|---|---|
| Shader library with compile-time constants (`shaders/XenosRecomp/shader_common.h`): `MASSEFFECT_UBO` is `true` at compile time, tfetch2D always takes 1/size from the constants, texture signs and vertex input remaps use `select()` instead of per-component branches. Pixel shaders average 18,668 → 9,928 SPIR-V words and 620 → 173 blocks; the library is 62 → 35 MB. NVK's compile time on the console scales with that size. | Swap `banjo_shaders.mesp` + `.idx`: the runtime works with both libraries (it still sets the UBO and 1/size specialization bits the old library reads). |
| First-boot prewarm list (`tools/make_prewarm_list.py`): the pipeline list of a long PC run, without the PC driver's cache data. | Delete `cache/masseffect_native_pipelines.bin` on the SD card. |
| `-fno-math-errno` on the generated game code (`BANJO_GENERATED_MATH_FLAGS`, masseffect-nx ships it, futari-nx checked it): no errno bookkeeping in math functions (sqrt is one instruction). IEEE results unchanged (no finite-math). **`-fno-trapping-math` was dropped on 2026-10-12** (sa2-nx's audit): it lets GCC run a float operation earlier than the code does (out of a branch or loop), and the recompiled code switches the host's flush mode with `msr fpcr` in between, which the compiler does not treat as something a float operation depends on, so an operation moved across a mode switch could treat a denormal operand in the wrong mode. Nobody observed a wrong result; the reasoning is sa2-nx's, and the flag bought sa2-nx at most 0.5 % of one file. Packages up to 2026-10-07_1659 were built with it. | `-DBANJO_GENERATED_MATH_FLAGS=OFF`; `-DBANJO_GENERATED_NO_TRAPPING_MATH=ON` puts the dropped flag back for a measurement. |
| **Optimised codegen** (2026-10-07, from sa2-nx, which took it from masseffect-nx): `app/renut_config.toml` keys `cr_as_local`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `skip_msr`, `dform_disp_split`, `fctiwz_inline` (guest condition/count/XER registers and the lwarx reservation as C++ locals, no MSR fences around guest atomics, fctiwz inline), and the post-codegen steps of `tools/post_codegen.py`: guest loads/stores without `volatile` (`pch_no_volatile.py`, masseffect-nx measured +15-20 % fps on the console) with a compiler barrier at every loop header (`loop_barriers.py`, REQUIRED: without it GCC turns guest polling loops into infinite loops), and direct calls between unhooked functions so GCC can inline them (`direct_calls.py`: 129,188 calls, 466 hooked addresses kept). `app/CMakeLists.txt` re-runs the steps after every codegen. The guest-visible result is the same; only the shape of the C++ changes. `tools/find_self_loops.py <elf>` must find no self-loops in recompiled functions (since the CPU rounds of 2026-10-13: none but `sub_82C25230`, explained in that section). | One NRO only (user rule): remove the keys from `renut_config.toml` and rebuild (the build regenerates and drops the steps). |
| **`fp_moves_modeless`** (2026-10-07, codegen option from sa2-nx's SDK round 7, copied into `banjo-nx/sdk`): a guest floating-point load, store or move no longer switches the host FPU between the scalar (IEEE) and VMX (flush-to-zero) modes with an `msr fpcr`; `lfs`/`stfs` convert with a helper that gives the IEEE result in either mode. Only arithmetic switches modes now. The guest-visible result is the same. Side effect: a function can now return in flush mode where its old epilogue `lfd` switched it back, so the native reverb (`banjo_reverb_native.cpp`) leaves the mode as the generated reverb does (`BANJO_FP_MOVES_MODELESS`, set by `app/CMakeLists.txt` from `renut_config.toml`); its verify compares the mode bit-exactly: 20,500 calls, 0 mismatching, whole verify session `differences: 0`. Switch ELF: `msr fpcr` 40,120 → 30,988 (−22.8 %), .text +553 KB (+0.9 %, the denormal-safe conversions inline); no self-loops in recompiled functions. | Set `fp_moves_modeless = false` in `renut_config.toml` and rebuild. |
| **`non_volatile_as_local` + `elide_lr_stores`** (2026-10-07, the other ports' measured "diet": masseffect-nx -10 % main thread, sa2-nx round 7, sasrt-nx): r14-r31, f14-f31, v14-v31 and v64-v127 become C++ locals, so the `__savegprlr`/`__restgprlr`/`__savefpr`/`__savevmx` helpers (35,300 static calls in the Switch ELF) and the guest-stack traffic for those registers disappear, and `ctx.lr = <return address>` is no longer stored before every call. Switch ELF: 0 helper calls left, .text 64.71 → 61.42 MB (−5.1 %), NRO 66.1 → 62.8 MB (builds/2026-10-07_0851_optimised); PC: renut.exe 64.4 → 56.4 MB; the verify session checked 2.5-4x more draws and audio blocks in the same 200 s. Audit (`app/renut_config.toml` has the details): (1) **setjmp**: 10 guest functions call it; `banjo-nx/sdk`'s codegen now copies every C++-local register into `env` at a setjmp and back after a longjmp (`function_graph.cpp`, `builders/context.cpp`), as the guest's longjmp does. (2) **ctx.lr** is read only by diagnostics; the guest unwinder is not implemented. (3) **`tools/read_before_write.py`** had two blind spots inherited from sa2-nx, now fixed: it never read named functions (`rex_*`; the CRT's SEH `__finally` funclets: `rex_LN15_18` crashed the first boot) and it dropped every vector read (every vector instruction writes through `simde_mm_store*`, which it took for a memory store). Fixed, it found: 70 new `share_registers` marks (39 fragments without a frame, 31 SEH funclets; a mark replaces the include's entry, so it repeats the name); and **registers this game uses outside the ABI**: VMX128 constants in v64-v77 built once by `sub_8239CE88` and read by ~900 functions (Microsoft's WMV decoder hands its asm routines permute masks in v64/v65 the same way: with locals every FMV was garbage on both renderers), and an implicit argument in v92 (`sub_82BD9408` → `sub_82BD8D68` → `sub_82BD59F8`). New SDK codegen keys: `non_volatile_as_local_keep = ["v64-v77", "v92"]` (those registers stay in ctx in every function; a save/restore helper covering one is called) and `non_volatile_as_local_exclude = [[start, end], ...]` (whole asm libraries keep the old code; the helper bodies keep ctx for them; used only for bisecting here). Residue, harmless: 26 normal functions whose own prologue saves the register they read early (infeasible paths through jump tables in loops; a read of an uninitialised variable would get 0 instead of the caller's leftover), data words in front of 491 functions, xor rX,rX,rX. Checked: verify session `differences: 0` (native reverb 63,000 calls bit-exact), videos of the FMVs, title, menus, the intro cutscenes and Spiral Mountain gameplay; no self-loops in recompiled functions. | Set both keys to false in `renut_config.toml` and rebuild. |
| **`fcti_inline`** (2026-10-12, codegen option from sa2-nx's second CPU round, copied into `banjo-nx/sdk`: `rex/ppc/diet_ops.h`, `builders/floating_point.cpp`): `fctid`, `fctidz` and `fctiw` through `rex::ppc` helpers instead of an isnan test, a range compare and simde's scalar conversion around each (Banjo: 425 `fctid`, 599 `fctidz`, 0 `fctiw`). **A correction for the Switch, not a measurable speed-up there (one compare fewer per site):** off x86, simde's `simde_mm_cvtsd_si64`/`si32` round with C `round()`, exact halves AWAY from zero (0.5 → 1, 2.5 → 3), where the Xbox 360 and the PC build round in the current mode, halves to even (0, 2). So every Switch build before this one rounded exact halves differently from the 360 and from the PC at the 425 `fctid` sites (310 of them are the `fctid` + `fcfid` "round to an integer" pair). The helper is `frinti` (current mode, FPSCR.RN is mirrored into FPCR) + the saturating `fcvtzs`. It also fixes simde's `si32` path answering INT32_MIN for a value that rounds up to exactly INT32_MAX. On x86 the result is unchanged except at exactly 2^63 (INT64_MAX as on the 360; `cvtsd2si` gave INT64_MIN). Self-check: every boot runs the helpers on 15 inputs (the halves, saturation, NaN) and logs `[enhancements] float-to-integer rounding self-check: 45 of 45 results as the Xbox 360's`: the AArch64 path cannot run on the PC, so the console log is its proof. Checked 2026-10-12. PC: `tools/read_before_write.py` output identical to the previous build's (535 lines); verify session `differences: 0`, 0 rejected draws; a 290 s recorded run (title, intro cutscenes, Spiral Mountain) normal. Switch ELF (builds/2026-10-12_2130_rounding against 2026-10-07_1659_sa2port): `fcvtas` (convert, ties AWAY: what GCC made of simde's `(int64_t)round(v)`) 435 → 12, none left in recompiled functions (the 12 are libm and Mesa); `frinti` 1 → 425, 422 of them in recompiled functions; no self-loops in recompiled functions; NRO size unchanged (62,896,325 bytes). Not yet run on a console. | Set `fcti_inline = false` in `renut_config.toml` and rebuild. |

## First console run of the optimised builds (2026-10-13) and what followed

The user's console session (`dist/switch/banjo-nx/newlogs2`, build 2026-10-13_1411, clocks 2397 / 1305.6 / 2733 MHz):
it boots and plays; about 15-18 fps. The forms self-check reported 248 of 248 on the AArch64 forms, NVK's per-draw
paths were requested and checked themselves (0 differing), no DIFFERENCE or MISMATCH line. The target is 30 fps at
1020 / 460 / 1600 MHz.

**Where the time goes (gameplay, per frame, at those clocks):**

| | measured | note |
|---|---|---|
| GPU, total | 56 ms | 30 fps at 460 MHz needs about 12 ms at 1305 MHz |
| GPU: EDRAM imports with 9 passes (depth + 8 stencil bits) | 20.6 ms | five whole-target imports a frame |
| GPU: other EDRAM imports | 9.8 ms | 24,552 depth imports in 10 s (142 a frame) |
| GPU: the scene | 16.7 ms | 1280x720 with 2x MSAA |
| GPU: copies, small targets, exports | 5 ms | |
| Ring thread | 50.5 ms of work per Swap | 2,715 draws at 18.6 us; the game issues about half: predicated tiling replays every draw |
| Ring thread: vertex data copied | 345 MB a second | the renderer cannot tell a static buffer from a changed one |
| Game threads | under 10 % of a core | the game waits for the renderer |
| One `msr fpcr` that toggles the flush mode | 18.8 ns (45 cycles) | above the 40 cycles `fp_mode_lookahead_past_branch = true` needed: it stays on |

A traced frame (`banjo_trace_frame_s`, PC): a 224x224 target; two shadow maps (1024x1024 and 768x768, about 210
draws each, each cleared through Direct3D's 4x-MSAA clear view); the scene at 1280x720 2x MSAA in two predicated
tiles (about 850 draws each, three resolve points per tile); a blur chain; the composite. All three depth targets
sit at EDRAM base 0 with different pitches, so every change between them is a possible carry-over of contents
unless the clear in between is recognised, and four clears a frame are declined by the redirect. That, and the tile
replay, is the work ahead (removing the emulation of what the game does not rely on, verified on the PC).

**`-mearly-ra=none` for the console build** (from peggle2-nx, 2026-10-13; `app/CMakeLists.txt`,
`BANJO_SWITCH_EARLY_RA`). devkitA64 GCC 16.1's AArch64 early register allocation pass (on at -O2 and above)
dropped table lookups in Peggle 2's copy of the XDK's unaligned streaming copy and overwrote a register holding a
constant: garbage in large textures on the console only. The trigger is the two-register table lookup of the NEON
vperm helper that came with sa2-nx's first CPU round. Banjo's ELF of 2026-10-13_1411 shows the routine complete
(`rex_XMemCpyStreaming_Unaligned_Cached_Helper`: 159 `tbl`) and no self-indexed `tbl` in any recompiled function, so
there is no sign Banjo was hit; the pass is off anyway. After a build: the flag on all 782 compile steps, 159 `tbl`
in that routine, 0 self-indexed. (peggle2-nx's first diagnosis, vector registers as by-value locals, was withdrawn;
the change made for it here was undone.)

**`banjo_native_tfetch_filters`** (correctness, non-CPU group, on). A Xenos texture fetch instruction carries its
own mag / min / mip filter and anisotropy, each of which may say "from the fetch constant". The native renderer
built its samplers from the fetch constant alone; the console and the emulated renderer use the instruction's.
21 shaders of the main menu override something (for example PS n139 fetch constant 1: point in the constant,
linear in the instruction). `ShaderEntry::tfetch_filters` (from the microcode at load), applied to the fetch
constant a draw's sampler is made from. Two fetches of one constant that disagree: the first one is used (logged).

**k_CTX1 decoded exactly.** CTX1 (two-channel normal maps) was rewritten block by block into BC1, which keeps 5 and
6 bits of the two 8-bit endpoints. It is now decoded to R8G8 with the emulated renderer's arithmetic
(`DecodeCtx1Block`: (2a + b) / 3 and (a + 2b) / 3 truncated; R in the high byte; host swizzle RGGG as the
emulation). Four times the memory of the BC1 form: 30 textures and 56 MB of texels in the main menu (14 MB before).
The log line `k_CTX1 textures decoded to R8G8` gives the total; if the console's memory needs it back, BC5 is not
exact either (its interpolation steps are sevenths and fifths).

**The dark patches on Banjo's fur: fixed (`banjo_native_stacked_baseless`).** They were in every native build
(a recording of 2026-10-05 has them) and never in the emulated renderer. Not vertex data, filters, CTX1 precision or
face culling (each ruled out with a RenderDoc capture of the same draw in both renderers, `tools/rdc_*.py`). The
fur's shell mask is a stacked texture, 128x128 with 16 layers, and the game streams it small levels first: while
the big level is not loaded the fetch constant says "lowest level 1" and its base address points at other data. The
native renderer built level 0 from that address. It now builds the texture from the first level present (two
textures in the main menu: `1C21C000` format 18 and `1B908000` format 19). `banjo_native_mip_range` came out of the
same search (the sampler's level range by the reference rule); it was not the cause.

## Removing emulation the game does not rely on (2026-10-13, evening)

What the console run showed: the GPU spends 30 of 56 ms importing EDRAM contents nobody reads, and the ring thread
draws the scene twice because Direct3D splits it in two predicated tiles. Four items, each with its own key (the
table above), each checked on the PC with an in-run A/B (`--banjo_direct_state_ab_s=2 --banjo_ab_target=N`, a
recording analysed with `tools/ab_flicker.py`: the picture must not change when the key flips).

**1. Clears the redirect declined.** A traced frame (`banjo_trace_frame_s` with `banjo_trace_frame_all`, which now
prints for every draw what the overwrite proof made of it and for every transfer who caused it) showed why targets
were still imported: the proof refused 16 clears a frame for a leftover tessellation register
(`banjo_gpu_proof_tess_leftover`), did not know the single-triangle full-screen passes (`banjo_gpu_proof_triangle`),
and colour clears went through the 4x view (`banjo_gpu_edram4_color_consumer`). Gameplay (Spiral Mountain), per 10 s
(300 frames) on the PC:

| | before | after |
|---|---|---|
| EDRAM tiles transferred | 1,394,632 | 311,400 |
| of them in nine-pass imports (depth + 8 stencil bits) | 546,600 | 4,800 |
| depth import operations | 49,328 | 26,400 |

Left, about 1,030 tiles a frame: a point-list draw before the scene whose vertex shader reads a texture (its extent
cannot be bounded: 720 depth and 112 colour tiles), a 64-tile strip at the edge of the first shadow map, small ones.

**2. The scene drawn once (`banjo_gpu_tiles_once`).** Direct3D renders the 1280x720 2x-MSAA scene in two tiles of
the same EDRAM rows: it sends the frame's command buffers twice, selecting a tile with `SET_BIN_SELECT` and giving
each tile its window (`PA_SC_WINDOW_OFFSET` and the window scissor) and its resolve destination under that tile's
bin mask. The native targets are not EDRAM: they can be as tall as both tiles. So during the first pass:

- a draw both tiles share is made once, with a scissor that covers both tiles' rows (the second tile's rows are the
  "spare rows" below the first tile's in the same host images);
- a draw or a command buffer of the second tile alone (it is predicated out in the first pass) is run right there,
  with the second tile's window moved down by the split;
- the first tile's own draws are clamped to its rows; a resolve is made for both tiles' rows, each to the
  destination its tile names; the clear that ends the pass also clears the spare rows;
- in the second pass every target-writing draw is compared, field by field (initiator, window, shaders, index
  buffer, surface and colour and depth state), with the one the first pass made for it, and dropped; its resolves
  are checked against the copies already made and dropped.

Anything else under a tile's mask (state other than the window and the resolve destination, a draw that cannot be
one draw or two, a layout that is not "second tile right below the first") sends that frame back to one pass per
tile, with a log line naming the reason. A second-pass draw that differs from its counterpart is a `DIFFERENCE` and
turns the key off for the session.

Measured on the PC: the main menu and gameplay both draw every scene once (300 of 300 per 10 s). Gameplay, per
frame: about 504 draws made for both tiles, 8 for the second alone, 512 second-pass draws dropped, every one matched;
0 `DIFFERENCE`; the A/B recording does not change at the flips and shows no seam at row 384. The console's ring
thread ran 2,715 draws a frame; in this scene that is about 510 fewer, and with them their vertex copies, state and
recording. (The ring's own draw counter still counts the dropped draws.)

**3. Static vertex buffers kept across frames (`banjo_ring_vertex_persistent`).** See the next section.

**4. The fur.** Above.

## Static vertex buffers kept across frames (`banjo_ring_vertex_persistent`, 2026-10-13)

The console copied 345 MB of vertex data a second. Measured on the PC in gameplay
(`masseffect_native_diag_vertices_repeated`, 10 s): 9,280 MB of vertex windows drawn, 64 % of them the same range and
bytes as earlier in the frame and the other 36 % the same as in an earlier frame. Every byte is a repeat, and 4,182 MB
were copied all the same (14 MB a frame), because the reuse that existed ends with the upload buffer (every frame)
and at every point where the game can learn of GPU progress (about 15 a frame).

Write protection cannot tell a changed buffer from an unchanged one here: on Horizon protecting guest memory means
unmapping the page and emulating every access in the exception handler. So the proof is by content
(`app/src/me/masseffect/banjo_vertices_persistent.h`):

- a table from (address, bytes, byte order, conversion) to a block of a persistent vertex buffer, "the arena". A
  window gets a block once its bytes have been the same in three frames (two confirmations after the first sighting);
- the first draw of a window after a synchronization point fingerprints all its guest bytes (XXH3 on the PC; the
  A57's two CRC32 polynomials with `masseffect_native_fingerprint_mode = 2`) and compares with the fingerprint taken
  when the block was written. Equal: the arena is bound, nothing is copied. Different: the block is given up and the
  window is copied to the upload buffer as before;
- until the next synchronization point the block stays proven (the rule the same-frame dedupe already relies on, with
  its sampled-fingerprint check for the small ranges), and so does every window inside it
  (`banjo_ring_vertex_containment`'s rule, in the arena);
- past a synchronization point it stays proven for the draws the game had already submitted when the bytes were read
  (`banjo_ring_vertex_persistent_submitted`): a draw's vertices are final when the write pointer passes it and stay
  untouched until the GPU has run it. The ring keeps a never-wrapping count of primary ring words for this;
- a window whose bytes changed twice is left alone for a while, longer each time, without being fingerprinted;
- the k_10_11_11 conversion (`banjo_repack_cached`) is part of the key, so converted windows are kept too (the
  same-frame dedupe never reused those);
- blocks are handed out in address order and never reused singly (the GPU may still be reading one). A full arena
  gives every block up and starts over once the three work slots have been waited for; if that happens three times
  within 600 frames each, the feature turns itself off and says so (raise `banjo_ring_vertex_persistent_mb`);
- vertex buffer bindings are now tracked per binding as (buffer, offset), so draws from the arena keep the base-zero
  binding (one `vkCmdBindVertexBuffers` per change of buffer, not per draw).

Not for: the transient ranges in Direct3D's command buffer pages, windows under 256 bytes, rectangle lists,
memexport streams (they have their mirror).

**Measured on the PC** (the PC is not the judge; `dist/switch/configs/banjo_vertex_persistent_ab.toml` gives both
halves on the console from one run):

| Gameplay, Spiral Mountain, per 10 s (300 frames) | before | after |
|---|---|---|
| vertex bytes copied | 4,182 MB (14 MB a frame) | 633 MB (2.1 MB a frame) |
| vertex bytes fingerprinted whole | (sampled only) | 3,529 MB (11.8 MB a frame) |
| windows bound from the arena | - | 380,000 (217,000 after reading their bytes, 163,000 already proven) |
| `vkCmdBindVertexBuffers` per draw | 0.11 to 0.13 | 0.20 |

Arena after the title, the menus, the intro and Spiral Mountain: 29.8 of 64 MB in 1,586 blocks; no window changed
in gameplay (a handful in the menus). Ring thread, in-run A/B over the whole 292 s session (`banjo_ab_target = 11`): 20.3 ms
of work per Swap without, 9.6 ms with. Almost all of that is the intro and its cutscenes (large windows drawn many
times a frame). In Spiral Mountain alone the PC shows no gain (10.8 ms with, 10.5 ms without): it reads 12 MB about
as fast as it copies 14 MB. On the console the copy also writes (into cached memory that is then published to the
GPU) where the fingerprint only reads, so a gain is expected there, but it has to be measured; the key is the way
back if it is not one.

**Checked.** `banjo_cpu_verify` run through the menus and the intro: 559,334 uses compared byte by byte with a
fresh conversion of the guest's bytes, 0 differences. Verify mode slows the ring to a few frames a second (on the PC
the arena is in write-combined memory, slow to read back), and then the game no longer runs ahead and the submitted
rule never applies. So there is a second check for the two rules that prove without reading:
`banjo_diag_vertex_persistent_check` fingerprints the guest's bytes again on every such use and compares with the
block's fingerprint, at full speed: 1,946,106 uses proven by interval and 289,810 proven by submission
fingerprinted again in a 292 s run, 0 differences.

**Not done: indices.** Their conversion (byte swap, minimum and maximum) is 1.45 us of a console draw against the
uploads' 3.5; the same table would serve them (a second arena bound as index buffer).

## Second console run (2026-10-13, 20:53) and the ring thread round after it

The eighth package on the console (`dist/switch/banjo-nx/newlogs3`, 86 s: title, menus and intro; clocks 2397 /
1305.6 / 2733 MHz): **26 to 30 fps** where the first run had 15 to 18. No `DIFFERENCE` or `MISMATCH` line; every
scene drawn once (`tiles once`: 250 to 295 scenes per 10 s, every dropped draw matched).

| per frame | first run | this run |
|---|---|---|
| GPU, busy | 56 ms | 24 ms (scene 15.4, EDRAM imports and exports 5.5, copies 2.2, 640-wide passes 1.2) |
| GPU, idle between submissions ("gap") | 1 ms | 13 to 14 ms: it waits for the ring thread |
| ring thread, work per Swap | 52 to 63 ms (the same scenes) | 31 to 35 ms |
| ring thread, share of one core | - | 95.6 % |
| game threads, all together | under 10 % of a core (they waited) | 83 % of a core (main 26 %, three workers 19 / 15 / 12 %) |
| "uploads" stage per timed draw | 4.7 to 5.9 us | 5.5 to 5.9 us |

So the ring thread alone sets the frame rate now, one core of three is idle, and
**`banjo_ring_vertex_persistent` gained nothing on the console**: the uploads stage costs the same with the copies
replaced by fingerprints (12.6 MB a frame at about 4.8 GB/s; the A57 copies about as fast as it runs two CRC32
polynomials). It stays on (it is not slower, and the bytes no longer cross the upload buffer), but it is not the
saving the PC's intro numbers suggested.

**Where the ring thread's time goes** (SDK sampler, 1,155 samples of the last 40 s, named with the tools below):
renderer code 66 %, the driver (Mesa/NVK) 17 %, C library 12 % (memmove 7.3 %, memcmp 1.5 %, malloc and free 1.7 %).
By function: `DrawsVulkanImpl::Draw` itself 11.2 % (spread evenly over the whole function: no hot line),
`RangeFingerprint` 7.7 % and `SampleFingerprint` 4.6 % (vertex windows), `Type3` 6.3 %, `WriteConstantRunRaw` 4.3 %,
`vk_dynamic_graphics_state_copy` 4.5 % (the driver, at every pipeline bind: 0.57 a draw), `IndicesFrom16` 2.5 %,
`Packet` 2.4 %, `MatchesVertexShaderIdentity` 2.3 %, XXH3 2.3 %, `nvk_bind_descriptor_sets` 1.8 %.

What was built from that (keys in the table above), all checked on the PC only:

- **`banjo_ring_shader_load_memo`.** 2,117,949 shader identifications in 2,095 frames: a thousand a frame went the
  whole way (three copies, an XXH3, a map lookup, and the identity cells recomputed at the draw). PC, main menu:
  582 loads a frame taken from the memo, 36 a frame still going the whole way (vertex programs whose fetch
  instructions Direct3D patches per declaration, and slot collisions); in-run A/B (`banjo_ab_target = 12`): the
  ring's PM4 and identification part 2.41 -> 2.09 us per draw.
- **`banjo_ring_vertex_epoch`.** Removes `SampleFingerprint` for everything outside the command buffer pages.
- **`banjo_ring_constants_adaptive`.** Removes the `memcmp` and one of the two copies of every constants upload
  while the compare is not finding anything.
- **`banjo_gpu_zcull_greater`** (off; `configs/banjo_zcull_greater.toml`). The scene's 15.4 ms is the largest GPU
  item left and its depth test runs in the direction the driver's ZCULL is not set up for. The log's "scene" figure
  in the "GPU per Swap" line says whether it helps; the picture has to be checked by eye.
- **Deferred recording as a console trial** (`configs/banjo_deferred_recording.toml`:
  `masseffect_native_deferred_recording` with its two companions). The driver's 17 % of the ring thread would move
  to the idle core. It was called broken for Banjo from PC runs only, where the worker's 20 us naps are a
  millisecond each (Windows timers); on the PC it still triples the ring's time, which says nothing about the
  console. `banjo_gpu_tiles_once` no longer stands down for it (the Vulkan calls are queued with their arguments by
  value, after the draw has read the registers; PC menu: 300 scenes drawn once per 10 s with it on, no difference).
  It cannot be switched inside a run: compare the "ring thread:" lines of two runs.

Looked at and left: the fingerprint on a helper thread (it would have to spin on the idle core at a priority above
the game threads'); `vk_dynamic_graphics_state_copy` (driver code; fewer pipeline binds would need reordering draws).
The game threads' own functions are the next piece of work (their profile is flat: the hottest, `sub_82949790`, is
1.6 % of all samples).

**Profile tools added** (`tools/`): `switch_hot_symbols.py` names the sampler's addresses from the ELF's symbol
table (so driver and C library code get names too, `--group` adds up by area); `switch_hot_stacks.py` names the
call chains of the hitch-frame stacks; `switch_hot_lines.py` gives source lines inside a function, for which the
build needs `-DBANJO_SWITCH_RENDERER_LINES=ON` (line tables for the three hot renderer files; the code and so the
addresses are the same as without it, so it names the samples of a run of the normal NRO).

## Render thread and GPU round of 2026-10-14

After the second console run the ring thread was the limit and the GPU's largest item after the scene was still
EDRAM transfers. This round: five changes on the ring thread and the GPU side, two measuring tools, and a survey of
what the other ports measured on the console. All verified on the PC: gameplay runs with 0 `DIFFERENCE`, and the
picture-affecting keys with an in-run A/B recording each (`tools/ab_flicker.py`: the flips do not stand out).

**EDRAM transfers, gameplay frame: 1,042 tiles -> 171.**

| change | tiles a frame |
|---|---|
| before | 1,042 |
| `banjo_gpu_dead_raster` (the particle simulation's point draws no longer pull the views they are bound to) | 197 |
| `banjo_gpu_rect_split` (the L-shaped rest of a 220x220 Clear: 42 -> 16) | 171 |

On the console the four transfer categories were 3.3 to 3.7 ms of GPU time a frame at 1305 MHz. What is left are
partial tiles: the 64 edge tiles of the first shadow map's clear strip (960..1024 of a 1040-wide surface, owned by
color views: the 16 columns beyond the map still belong to someone), the 48 of the second shadow map's last column,
and a few of the small targets' last rows. Taking those without a copy would need ownership of parts of a tile.

Tried and taken out again: the color half of a depth + color clear taking partly covered edge tiles in their RGBA8
owner (8 tiles a frame of a 48x128 clear). The clear was redirected, and the next draw into that target pulled the
same 8 tiles: the transfers per frame were the same to the tile.

**Ring thread.** `banjo_ring_shader_load_memo` with 4,096 slots instead of 1,024 (stored loads 45,077 -> 13,222 per
10 s in gameplay: the rest were slot collisions), `banjo_ring_identity_once`, `banjo_ring_sampler_fast`,
`banjo_ring_vertex_proven_first`. Small each; their sum is what the next console profile shows.

**Measured, for the next rounds.**

- *The second tile pass.* With `banjo_gpu_tiles_once` its draws are dropped, but the stream is still parsed, its
  constants written and its shaders loaded: 12 to 13 % of the first pass's ring time on the PC (the "tiles once"
  report line now prints both). Skipping its buffers whole needs a proof that the state they start from is the
  first pass's, not only that their bytes are.
- *Resolves.* 30 a frame; per tile the scene resolves color + depth three times. The middle color resolve (after
  the water, before the transparent draws) is never sampled before the last one overwrites it: 1 of 3 full-screen
  2x color resolves. Not skipped: whether a later draw samples it is only known later, so an exact skip needs
  command buffer segments that are left out afterwards; a learned skip would show a stale frame the first time a
  refracting object is drawn there.
- *The scene pass by pixel shader* (`banjo_diag_gpu_groups = 2`, PC): no shader stands out; the largest scene
  group is 13 % (a full-screen pass), alpha-tested groups together about 16 %. The PC's proportions are not the
  console's (there the scene is 65 to 75 % of GPU time and shadow maps are small; on the PC shadow maps are the
  largest): `configs/banjo_gpu_groups.toml` is for a console run.

**Sonic Unleashed's GPU list** (`UnleashedRecomp-NX/docs/SWITCH-PERFORMANCE.md`), checked against this renderer:

| there | here |
|---|---|
| constants through uniform buffers; texture sizes from constants; `max(a, a)` as `a`; one `if` per run of predicated instructions; blended pixels that change nothing discarded; depth-only pipelines without a fragment stage; unread varyings dropped (driver) | already here (from nfsmw-nx / masseffect-nx / sa2-nx) |
| GPU timer 1.627 ns per tick | not the case here: GPU busy + idle add up to the frame time |
| ZCULL in `greater` mode | `banjo_gpu_zcull_greater`, console trial (it gave them 0.17 ms) |
| alpha-test early-out | nothing to skip: 302 of 328 single-exit pixel shaders write alpha in their last statement |
| alpha-test sinking (their largest shader gain: foliage was 8 of 14.5 ms) | not ported: alpha-tested draws are about a sixth of the scene here, over many shaders |
| resolve hand-over (the texture takes the target's image) | not possible: EDRAM views are as wide as the pitch (1040 for a 1024 map) and the scene is a 2x sample grid |
| copy-on-change resolves | the scene's three resolves per tile each follow depth-writing draws |
| dead copies, carried clears, skipped depth clears, early depth transitions | off there (device lost in long sessions); not taken |
| texture descriptor heap of 64 KB | this renderer binds a descriptor set per draw, no heap |
| resolution scale, MSAA | not wanted (the user, 2026-10-14) |

Sonic Transformed had nothing console-measured to add, and passed on that futari-nx found deferred recording not
pixel-identical: `configs/banjo_deferred_recording.toml` says so.

## sa2-nx's CPU rounds (ported 2026-10-13)

sa2-nx made three rounds of changes to the recompiled game code and the runtime on 2026-10-12 (its
`docs/cpu-rounds.md`; the first round is sasrt-nx's). Its console result: version 1.0.1 against 1.0.0 "better by
about 15 %" by the user's estimate, in one build that also had a new PGO profile and a link setting, so the rounds'
own share is not separated (sa2-nx's estimate for them was about 5 % of the game thread). Banjo took all three at the
user's request; **LTO and PGO are left for the release build** (the user's decision; `app/src/banjo_pgo.cpp` is
written and not compiled in). Everything below is verified on the PC only; **nothing of it has run on a console.**

**Why it should matter more here than in SA2.** The PC census (`banjo_diag_runtime_census = true`, Spiral Mountain):

| per second, PC | Banjo | SA2 | sasrt-nx |
|---|---|---|---|
| flush-mode switches (each an `msr fpcr` on the A57) | 868,000 with the lookahead and without its bet; 540,000 as built | 80,400 | 1,400,000-2,100,000 |
| indirect calls | 214,000 | 1,200,000-1,360,000 | - |
| critical sections entered | 24,000 | 25,700 | 85,000 |

**What was ported.** The SDK changes are sa2-nx's three commits applied as patches to `banjo-nx/sdk` (Banjo's own
generator features, the setjmp locals and `non_volatile_as_local_keep` / `_exclude`, are unchanged), with its unit
tests (`sdk/tests`, `tools/run_sdk_tests.bat`: 9 of 9 pass, 17.4 billion conversion checks, 0 differences). The
post-codegen tools are sa2-nx's files; `tools/post_codegen.py` runs them in sa2-nx's order on Banjo's one tree.

| Step | Where | Banjo |
|---|---|---|
| Flush-mode checks the control flow fixes (nothing on the Switch) | `tools/flush_mode_flow.py`, with the entry modes from the image | 8,937 of 21,771 checks proven; 959 functions entered in a known mode |
| Comparisons and conversions that need no mode; a lookahead before each switch | generator: `fp_compares_modeless`, `fp_mode_lookahead`, `fp_copy_pairs` | see "The lookahead's bet" below |
| ctx registers of call-free loops as locals for the length of the loop | `tools/loop_locals.py` | 10,180 loop regions in 4,717 functions, 56,598 registers |
| ctx registers of leaf functions with loops as locals | `tools/leaf_locals.py` | 1,406 functions, 17,316 registers |
| Loads from the image's read-only data as constants | `tools/ro_constants.py` (needs `out/renut_image.bin`) | 14,109 float, 1,719 double, 478 integer, 1,850 vector loads |
| A single-precision load straight into a float register | `tools/float_loads.py` | 24,329 loads |
| Guest vector byte reversal without a table lookup; constant shuffles as one permutation | `tools/vec_byteswap.py`, `tools/pch_neon_shuffles.py` | 53,881 reversals, 9,101 table shuffles |
| vperm, vctsxs, vctuxs and ten small VMX helpers as NEON instructions | `rex/ppc/intrinsics.h` | Switch only |
| Compare-and-swap without the legacy builtin's trailing `dmb ish` | `tools/guest_cas.py`, `rex/thread/atomic.h`; run time: `banjo_cpu_guest_cas_no_barrier` | 559 sites |
| Indirect calls with one to three usual targets as guarded direct calls | `tools/indirect_calls.py`, `app/indirect_promotions.txt` (from `banjo_diag_indirect_census`) | 15,766 sites tagged; 187 promoted, catching 57.9 % of the census's calls |
| Loop barriers only where a guest load could be hoisted | `tools/loop_barriers.py` | 7,435 of 36,506 loop headers need none |
| Integer divisions as the instruction | generator: `div_inline` | Switch: `sdiv` / `udiv`; PC: the expression as before |
| The trap helper out of line and cold | `tools/pch_cold_traps.py` | a layout change |
| No frame pointer in the game code | `BANJO_SWITCH_OMIT_FRAME_POINTER` (Switch) | 27,354 game functions set one up before |
| Critical sections take their owner from the PCR | SDK key `kernel_critsec_pcr_owner` | 24,000 enters a second |
| No notify for an event or semaphore nobody waits on | SDK key `thread_signal_skip_idle` (Switch) | - |
| Pointer translation without a heap lookup, object reference counts and `kernel_state()` inline | SDK | - |
| Every start checks the forms only one processor has | `app/src/cpu/banjo_forms_selfcheck.cpp` | 248 results; replaces the rounding check of 2026-10-12 |

Not taken, as in sa2-nx: `rotate_word_narrow` (no gain with the Switch compiler), `fp_copy_only_functions` (lost in
its census), `dcbt_prefetch` (unmeasured), and the three stack-frame elisions of `tools/frame_traffic.py`, which
only marks its statements here: `BANJO_GEN_LR_MOVES_ELIDE`, `_NV_SAVES_ELIDE` and `_BACK_CHAIN_ELIDE` are OFF
because sa2-nx found them **not exact** (a game reads a stack word it never wrote). sa2-nx's localized native copies
do not exist in Banjo.

**The lookahead's bet.** `fp_mode_lookahead_past_branch` (sasrt-nx's rule: also use the any-mode form of a conversion
when nothing that needs a mode follows before the next branch) lost in SA2, whose float code is scalar. Measured
here on the PC, the last 10 s of the same session:

| per second | flush-mode switches | any-mode conversions | any-mode compares | flush checks left in the code |
|---|---|---|---|---|
| `fp_mode_lookahead_past_branch = false` (sa2-nx's setting) | 868,000 | 950,000 | 87,000 | 17,947 (12,280 more proven) |
| `= true` (sasrt-nx's rule) | 540,000 | 4,100,000 | 700,000 | 12,834 (8,937 more proven) |

38 % fewer switches for 3.8 million more helper tests a second: 11.5 tests per switch saved (SA2: 235). The switches are in maths functions that alternate scalar and vector arithmetic (`sub_8294EF68`, `sub_823B59C0`, `sub_823B4C78`, `sub_82CE9250`; `tools/census_sites.py` names them), the code the rule was written for, so it is **on** here. That is a judgement, not a measurement: if every test were extra work, the break-even is an `msr fpcr` of about 40 cycles; it is lower where a test replaces a mode check, and the whole difference is about 1 % of one core either way. The console's profiler log now measures the cost of one switch ("FPCR on this CPU", from sa2-nx); `fp_mode_lookahead_past_branch = false` and a rebuild if it says otherwise.

**Switches.** Generator options in `app/renut_config.toml`. Build options in `app/CMakeLists.txt`, each OFF compiles
the statements as before: `BANJO_GEN_RO_CONSTANTS`, `_FLOAT_LOADS`, `_LEAF_LOCALS`, `_LOOP_LOCALS`, `_COLD_TRAPS`,
`_PROMOTED_CALLS` (every platform) and `BANJO_SWITCH_OMIT_FRAME_POINTER`, `_FLUSH_FLOW`, `_GUEST_CAS`,
`_VEC_BYTESWAP`, `_NEON_SHUFFLES`, `_NEON_VMX_HELPERS` (Switch). Run time: `banjo_cpu_guest_cas_no_barrier`,
`kernel_critsec_pcr_owner`, `thread_signal_skip_idle` (CPU group; `configs/banjo_cpu_rounds_off.toml` turns the
three off).

**Self-checks.** The Switch build has no code at these places; the PC build keeps the old operation next to the new
one and compares on every execution, and a wrong one counts as a difference of the verify session:
`[verify] flush-mode flow: N checks proven redundant executed so far, 0 had to switch the mode` and
`[verify] generated-code self-checks so far: N loads folded to read-only constants, N promoted indirect calls, N
critical section owners from the PCR; 0 differences`. The loop and leaf locals, the float loads and the
compare-and-swap have no run-time check: they are exact by construction (sa2-nx's argument and its lockstep frame
comparison; the float loads and the any-mode helpers have unit tests). Banjo has no lockstep frame comparison, which
is what found sa2-nx's one wrong step (the stack-frame elisions, off here).

**Verification (PC, 2026-10-13).** Unit tests 9 of 9. Verify session (200 s, every self-check on every use): `differences: 0`, 0 rejected draws, forms self-check 248 of 248, 730 million proven flush checks executed and none wrong, 595 million folded constants, 54 million promoted calls and 18 million critical-section owners compared and none wrong. A 290 s recorded run (movies, title, menus, intro cutscenes, Spiral Mountain): its last frame at full size matches the previous package's (shadows, lighting). A second run of the post-codegen sequence changes no file. Switch ELF (builds/2026-10-13_0325_cpurounds against 2026-10-12_2130_rounding; `tools/arm_cost_census.py`): game functions that set up a frame pointer 27,354 → 16; `msr fpcr` sites in the game code 28,700 → 13,796; `dmb` in the game code 655 → 658, each now behind the run-time key's test; game code 7.98 → 7.82 million instructions (−2.0 %), loads −4.2 %, stores −0.8 %, calls −4.3 %; `sdiv` / `udiv` 1,710; no `fcvtas` in recompiled functions; NRO 62,896,325 → 62,175,429 bytes. Two things the ELF checks flag and why they are right: (1) `tools/find_self_loops.py` now finds ONE self-branch in a recompiled function, `sub_82C25230`: a ring-index walk on registers only (no guest load, no call: one of the 7,435 loops that lost their barrier) which the compiler proved endless for a one-slot ring with a non-zero target; the Xbox 360 code spins there too, and nothing another thread does could end it. (2) one vector `fmla` in `sub_8235ADB8`: two adjacent `fmadds` of the guest (fused on PowerPC, `std::fma` in the generated code) vectorised together now that their registers are loop locals; not a contraction (`-ffp-contract=off` is unchanged).

**Banjo-specific points.**
- `tools/flush_mode_flow.py` reads the hook lists of `app/config` but not `config/renut_funcs.toml` (every function
  boundary: it would make every function look hooked).
- `tools/loop_locals.py` leaves alone any loop that calls a name it does not know, which covers Banjo's mid-asm
  hooks; functions with setjmp / longjmp are left alone by both locals tools.
- The registers of `non_volatile_as_local_keep` (v64-v77, v92) stay in ctx between loops as before; inside a
  call-free loop they are locals like any other ctx register.
- The image dump the tools read is game data (`out/renut_image.bin`, written by the generator when
  `REXGLUE_DUMP_IMAGE` names it; `tools/pc_env.bat` sets it): never committed or distributed.
- The code generator no longer re-runs on every build: `codegen_flavor.txt` is written only when it changes
  (sa2-nx's fix).

**Also from sa2-nx's 1.0.1: `banjo_native_partial_resolve_inside`** (correctness, non-CPU group, on).
`SearchResolvedPartial`, which sa2-nx took from banjo-nx (Nuts & Bolts resolves one texture in several rectangles,
each with the destination address advanced to its tile), accepted a resolve whose destination merely starts inside
an older resolved texture, also after the game had freed that one: a whole new texture a few pages above it was drawn
into the old texture and the draws sampling the new address read guest memory (black speckles on SA2's console). A
resolve is now such a rectangle only when it declares that texture's size and lies inside it. Banjo's own tile
resolves declare the full size and still match (they declare the texture's height, or, for the 2048x2048 shadow atlas resolved as 16 rectangles, the rows left below the advanced base. sa2-nx's rule knew only the first and, ported as written, turned the atlas's lower three bands into textures of their own: nine refusals in the PC log, no self-check reporting it, a contact sheet that looked normal. Banjo's rule accepts both heights; after it, 0 refusals and the same 18 `partial resolve` lines as before the fix).

**Long sessions: `banjo_native_resolved_retire_idle`** (non-CPU group, on; from sa2-nx's
`sa2_native_resolved_retire_idle` of 2026-10-13, with two Banjo additions).
- **The bug** (reports of sa2-nx 1.0.0 and nfsmw-nx on this renderer; not yet seen in Banjo, which has had no long
  console session): after one to two hours the shadows glitch, then textures turn black and geometry goes missing
  until the game is restarted. `resolved_` kept the image of every address ever resolved to. The game frees a render
  target's texture when a scene ends and the next scene allocates its own elsewhere, so they pile up until GPU memory
  is full. Banjo: 43 addresses after 290 s on the PC (movies, title, menus, intro, Spiral Mountain); one scene holds
  about 33 MB, among it a 2048x2048 shadow atlas and several 1280x720 copies. A second effect for tiled textures: a
  fetch at an address still in `resolved_` is served from the old image, so an ordinary texture loaded later at a
  freed render target's address would be drawn from it.
- **sa2-nx's fix:** each entry records the presented frame of its last resolve and last read; about once a second,
  entries with neither for `banjo_native_resolved_idle_frames` (3,600: two minutes at 30 fps) are released, the
  parked shapes too; an entry with a pending front copy or an EDRAM tile owner stays; a failed GPU allocation makes
  the next Swap release everything idle for 120 frames.
- **Banjo's additions, both found by the fix's own report line on the PC:**
  1. A later rectangle of a texture (`SearchResolvedPartial`) and a parked shape served to a fetch count as a
     resolve and a read of that texture (sa2-nx has neither path in use).
  2. **Textures the game keeps reading long after drawing them are not released when idle.** Nuts & Bolts draws a
     256x256 texture once (194D1000 / 19460000) and reads it for the rest of the scene, and it redraws its 2048x2048
     shadow atlas only when something moves: the title scene read it 3,900 frames after its last resolve. Neither
     is drawn again by the game on its own, and nothing is ever resolved into guest memory, so releasing one while
     a long pause hides the scene would lose it for good. An entry that is read 600 presented frames or more after
     its last resolve is marked kept (`is kept by the game` log line) and is released only when it sat idle for the
     configured time AND memory is short: a failed allocation, or the resolved textures holding more than
     `banjo_native_resolved_budget_mb` (128; about four scenes).
- **What is still open:** a texture drawn once and first read more than two minutes later would be released before
  that read (not seen; the PC build logs `fetch at X, whose resolved texture was released` if it happens). And the
  console running out of memory has not been shown for Banjo: only a long session there tests it.
- **Tests (PC):** idle time 120 frames (four seconds) and a budget of 16 MB over the 290 s session: 28 textures
  released, the needed ones created again (77 creations instead of 49), both kept textures recognised, 0 fetches at
  released addresses, 0 rejected draws, the last frame as before (shadows, minimap). Defaults: verify session
  `differences: 0`, nothing released within 200 s.
- **Log (every 600 presented frames, console too):** `[native] targets: N resolved textures and N parked hold N MB
  (N of them kept by the game long after drawing, N KB); N released when idle so far ...; the oldest read came N
  presented frames after its resolve`. With the key off this line is the growth of a long session.
- **Off:** `banjo_native_resolved_retire_idle = false`.

**Not ported, with reasons.** `sa2_vsync_relaxed` (no fall from 60 to 30: the vblank comes when the flip waits for
it): it stops the regular vblank ticks while a frame is late, so a game that measures time in vblanks would slow
down, and how Nuts & Bolts queues its flips and counts time has not been read yet; it needs that analysis and a
console log that shows the game sitting between two frame rates. `sa2_gpu_render_scale` / `sa2_gpu_msaa`: features, not exact. `sa2_gpu_prewarm_current_keys`:
Banjo's list already holds only current keys. `sa2_cpu_hook_counter_ctx`: SA2's per-hook use counters; Banjo's hot
hooks have none. sasrt-nx's `sasrt_cpu_index_no_fingerprint`, `sasrt_cpu_ring_draw_trims` and
`sasrt_api_thread_stats`, which sa2-nx had nothing for (no indexed draws, one drawing thread), may fit Banjo
(indexed draws, four drawing threads): candidates, not looked at yet.

## Switch runtime fixes from sa2-nx (2026-10-07, `sdk/src/core/threading_posix.cpp`)

- **Thread reaper:** libnx has no thread detach, so a thread nobody joins keeps its kernel slot and its 16 MB stack. SA2's one-shot job threads piled up to the 95-thread limit and `CreateThread` failed (C0000017; a loading screen then waited forever). Each finishing thread now claims its join and a reaper thread joins it a second later; when a creation fails, finished threads are joined at once.
- **Stack pool:** reaped 16 MB stacks went back to a fragmented heap; new guest threads failed with "heap has NO a 16448 KB block" while 1+ GB was free and waited for joins (0.3-1 s frames, 74 s in one SA2 console run). Guest stacks (8 MB and up) are now blocks of their own (`pthread_attr_setstack`), up to 4 (80 MB) kept for the next thread.
- On the console look for: thread creation failing with C0000017, "heap has NO a" lines, the thread count climbing in `rex_profile.log`, second-long frames at level loads. Switch only (the PC uses Windows threads).

## Correctness fix (not an enhancement)

**`banjo_ring_sync_fences` (2026-10-07).** The same-frame reuse of vertex and index copies (the vertex dedupe, its
containment, the index cache) is exact only because the game cannot rewrite memory a draw already referenced without
first learning that the GPU is done with it, and the caches forget everything when `g_synchronizations_ring` moves.
Nothing ever moved it (masseffect-nx's native frontend never published its wait generation), so only the frame
boundary cleared them. The ring now moves it, in stream order, at every point where the CPU can learn of GPU progress:
fence and query writes (EVENT_WRITE_SHD/ZPD), memory writes (MEM_WRITE, REG_TO_MEM, COND_WRITE when it writes),
interrupts and swaps (sasrt-nx's set, less EVENT_WRITE_EXT: Direct3D's per-draw tiling extents, not a fence; with it
the reuse all but stopped). And command buffers are recycled by the ring's read pointer or by re-recording, not
necessarily behind a fence, so ranges inside the 4 KB pages the ring ran as indirect buffers (any depth) or as the
primary ring (UP-draw data written inline) are always fingerprinted, never matched by address
(`g_command_buffer_pages`). On by default and outside the master switches (it is correctness). verify: 0 differences.

`app/CMakeLists.txt` now compiles the app and the recompiled game code (`renut`, `renut_recomp`) with
`-ffp-contract=off -fno-strict-aliasing`, as the SDK already did for its own libraries. Without it GCC (Switch) fused
separate guest multiplies and adds into FMAs across statements (its default is `-ffp-contract=fast`), so the game's
floating point on the console differed from the Xbox 360 and from the PC build (x86-64-v2 has no FMA). The native
game-function replacements are checked against the recompiled code built this way.

## Hot guest functions as localized copies (2026-10-14, the mechanism of sa2-nx)

The request: profile the game on the PC, optimise its 40 hottest functions for the A57, verify that they give the same
results, test on the console afterwards. sa2-nx did this (its `app/src/cpu/sa2_hot_guest.cpp`, `tools/localize_native.py`,
GPL-3.0 like this project; it took the guard from masseffect-nx and the idea of the copies from UnleashedRecomp-NX).
Its mechanism is ported here. **What it buys in Banjo is small, and the reason is measured below: 18 functions run
as copies, not 40.** Nothing of this has run on a console.

### What a copy is

`tools/localize_native.py` writes, for every function of `app/src/cpu/hot/localized.txt`, a header
`app/generated/hot/n_<id>.h` with the recompiled function's own statements, changed in three ways:

- the guest registers the function keeps in the context (the volatile ones: r0, r3-r12, f0-f13, v0-v13) are C++
  locals, loaded at entry and written back at a return;
- the functions it calls directly come along as copies of their own when their whole call tree can be repeated by
  the guard (no indirect call, kernel import, hooked function, mid-asm hook, compare-and-swap, memory barrier, MMIO
  or time base anywhere under them) and is small. The compiler must inline those (`always_inline`): a guest call
  inside the copy is then no call, and constant arguments reach the callee's code;
- a call that stays a real call stores only the registers its callee can reach and that can have changed, and loads
  back only those the callee can change (sa2-nx's "smart sync").

`app/src/cpu/banjo_hot_guest.cpp` puts a hook with its own key in front of each
(`banjo_cpu_native_<function>`, CPU group, so `banjo_cpu_enhancements = false` turns all of them off; each is in the
`[enhancements]` line). `tools/post_codegen.py` runs the tool after every codegen, so a copy always matches the
function the build compiles, and `tools/direct_calls.py` sends the callers of the listed functions through the hooks.

### What is different from sa2-nx

- **The guard needs no list of written memory.** sa2-nx's guard takes, per function, a hand-written list of the guest
  ranges it writes, which is why only its leaves are guarded and its copies that call other functions run unguarded. Here every
  copy exists a second time with each guest store recorded first (address, length, the bytes it replaces), and so do
  the ORIGINAL statements. A guarded call runs both recorded forms on copies of the registers and rolls the memory
  back from the record; the bytes either wrote are the compare set; then the real copy and the real recompiled
  function run over a snapshot of exactly those bytes, and every register of the context (all of them, not a chosen
  set: a copy writes back exactly what its original changes), the written bytes and the host's flush mode are
  compared. So a copy that calls other functions is guarded too, as long as the call it makes is to a copy.
- **A recorded form stops at a call it cannot repeat** (anything outside the copy). That call of the function is
  "not comparable": in verify mode the recompiled original answers it, otherwise the copy, and the log counts it per
  function. A function none of whose calls is comparable is not verified and is not in the list.
- **Only what can have changed is written back.** A forward data-flow pass over the function's jumps gives, for
  every return and every call, the registers whose local can differ from the context there. Without it a path that
  changes two registers paid the stores and loads of all forty.
- **A difference must repeat.** The audio functions read buffers the decoder thread is filling: the guard's runs saw
  different input and reported a difference although neither version was wrong (first verify run: `sub_82B0DCB8`
  and the vector SRC, one each in 45,000 and 100 calls). On a difference the inputs are put back and the whole pass is
  made again, three in a row at most: a wrong copy differs every time, a race does not. Those are counted apart
  ("did not repeat").
- **The hook costs 14 A57 instructions** (one byte "is it on", one counter word in the calling thread's own context
  that counts down to the next guarded call, the jump): no `thread_local` (a call to `__aarch64_read_tp` on the
  Switch), no counter shared between threads.
- **The loaded guest code is hashed** over the function and every callee its copy contains (sa2-nx: the function);
  the hash is in the generated header.

Guard defaults: the first 64 guarded calls of each thread, then one call in 4,096 (`banjo_hot_guard_calls`,
`banjo_hot_guard_period`); every call with `banjo_cpu_verify`. A difference logs `[hot] DIFFERENCE <function> ...`,
counts into the verify line's `differences:` (`me::native::VerifyDifference`) and switches that copy off for the rest
of the run; the original had already produced the state. Every 10 s (when guarded calls happen) the log has one
`[hot] <function> ON: about N calls so far, N compared with the original, N not comparable, N differences, ...` line
per function reached and a `[hot] native copies: ...` summary.

### How the functions were ranked

PC profile (`banjo_pc_profile_s = 20`, new key `banjo_pc_profile_top` for more than 30 lines per thread), guest
functions on the game threads only, waits left out: 12,409 samples in 80 s of Spiral Mountain, 36,231 in 200 s of
intro cutscenes, 2,954 in 60 s of main menu. Shares below are of those samples; the rank weighs gameplay 0.5, intro
0.3, menu 0.2. The console profile of 2026-10-13 (`newlogs3`; title, menus and intro, no gameplay) has 939 guest
samples on the game threads in all, so beyond its first few functions it cannot rank anything: it is the "console
samples" column. Where the two disagree: `sub_82949790` is 4.5 % of the console's samples and 1.2 % of the PC's
(a worker thread there, the main thread here; its sibling `sub_8294EF68`, first on the PC, has no console sample),
and the audio thread's `sub_82B0DCB8` is 3.7 % on the console against 1.6 %.

| Rank | Function | PC share %: gameplay / intro / menu | Console samples (of 939) | What became of it |
|---|---|---|---|---|
| 1 | `sub_8294EF68` | 5.33 / 4.09 / 0.00 | 0 | copy built, left out: +22.1 % A57 instructions per call |
| 2 | `sub_82948928` | 4.46 / 3.51 / 0.00 | 25 | copy built, left out: +54.4 % A57 instructions per call |
| 3 | `sub_823A9E80` | 3.72 / 2.61 / 2.74 | 9 | runs as a copy (-3.8 % A57 instructions per call) |
| 4 | `sub_8222F410` | 2.21 / 1.56 / 2.54 | 8 | no copy: a mid-asm hook inside |
| 5 | `sub_823B59C0` | 1.68 / 1.00 / 4.54 | 3 | copy built, left out: -1.1 % A57 instructions per call |
| 6 | `sub_823A9FF0` | 2.08 / 1.68 / 1.46 | 2 | runs as a copy (-4.7 % A57 instructions per call) |
| 7 | `sub_82BB4510` | 1.29 / 2.07 / 1.83 | 7 | no copy: its registers are locals already (leaf with loops) |
| 8 | `sub_829A6DE8` | 2.26 / 1.65 / 0.00 | 16 | no copy: its registers are locals already (leaf with loops) |
| 9 | `sub_8222A0A8` | 1.68 / 1.25 / 1.96 | 9 | copy built, left out: +6.4 % A57 instructions per call |
| 10 | `sub_82B0DCB8` | 1.60 / 1.44 / 1.73 | 35 | runs as a copy (-4.8 % A57 instructions per call) |
| 11 | `sub_826567C8` | 1.72 / 1.14 / 1.62 | 10 | no copy: the renderer hooks it |
| 12 | `sub_8222BF10` | 1.62 / 1.12 / 1.25 | 10 | no copy: the renderer hooks it |
| 13 | `sub_8222A408` | 1.50 / 1.02 / 1.66 | 5 | copy built, left out: +20.8 % A57 instructions per call |
| 14 | `sub_82226730` | 1.61 / 1.02 / 1.22 | 4 | copy built, left out: +0.2 % A57 instructions per call |
| 15 | `rex_XAudioMixFrameBufferChannel` | 1.27 / 0.84 / 2.23 | 20 | no copy: its registers are locals already (leaf with loops) |
| 16 | `sub_82BDAED0` | 1.75 / 1.51 / 0.00 | 9 | copy built, left out: +2.7 % A57 instructions per call |
| 17 | `sub_823A9C38` | 1.20 / 0.99 / 1.05 | 2 | copy built, left out: never comparable |
| 18 | `sub_8222EFA8` | 1.47 / 0.87 / 0.54 | 4 | no copy: its registers are locals already (leaf with loops) |
| 19 | `sub_8222F0D8` | 1.10 / 0.80 / 1.35 | 5 | no copy: its registers are locals already (leaf with loops) |
| 20 | `sub_8237B1D8` | 1.42 / 1.08 / 0.00 | 8 | copy built, left out: +2.4 % A57 instructions per call |
| 21 | `sub_8222D0E8` | 1.19 / 0.79 / 0.91 | 8 | copy built, left out: +4.1 % A57 instructions per call |
| 22 | `sub_823A74E8` | 1.09 / 0.72 / 0.85 | 5 | runs as a copy (-26.2 % A57 instructions per call) |
| 23 | `sub_8222CB90` | 0.86 / 0.83 / 1.12 | 4 | copy built, left out: +7.3 % A57 instructions per call |
| 24 | `sub_82949790` | 1.16 / 0.91 / 0.00 | 42 | copy built, left out: +22.1 % A57 instructions per call |
| 25 | `sub_823A9B18` | 0.87 / 0.52 / 1.05 | 3 | copy built, left out: only reached inside a caller's copy |
| 26 | `sub_8222AB88` | 0.90 / 0.63 / 0.78 | 3 | runs as a copy (-9.9 % A57 instructions per call) |
| 27 | `sub_82951B78` | 1.16 / 0.71 / 0.00 | 1 | copy built, left out: never comparable |
| 28 | `sub_8230AE18` | 0.52 / 0.46 / 1.79 | 3 | runs as a copy (-6.4 % A57 instructions per call) |
| 29 | `sub_82BB4A40` | 0.56 / 0.45 / 1.42 | 2 | copy built, left out: +0.0 % A57 instructions per call |
| 30 | `sub_8230A690` | 0.28 / 0.33 / 2.20 | 0 | copy built, left out: never comparable |
| 31 | `rex_WMVideoDecDecodeP_X9_2Pass_EMB_Pass1` | 0.00 / 2.24 / 0.00 | 4 | movie / decompression code: not menu or gameplay |
| 32 | `sub_8225A188` | 0.53 / 0.42 / 1.39 | 3 | copy built, left out: never comparable |
| 33 | `rex_fast_decode_aligned_offset_block_XCOMPRESS__YAJPAUt_decoder_context_1_JH_Z` | 0.00 / 2.20 / 0.00 | 0 | movie / decompression code: not menu or gameplay |
| 34 | `sub_823B4C78` | 0.53 / 0.31 / 1.46 | 8 | no copy: its registers are locals already (leaf with loops) |
| 35 | `sub_823AB4A8` | 0.49 / 0.35 / 1.39 | 3 | runs as a copy (-12.0 % A57 instructions per call) |
| 36 | `sub_823B49A8` | 0.47 / 0.35 / 1.39 | 1 | copy built, left out: +0.2 % A57 instructions per call |
| 37 | `sub_82CE9250` | 0.83 / 0.66 / 0.00 | 13 | copy built, left out: +0.6 % A57 instructions per call |
| 38 | `sub_8225B330` | 0.39 / 0.36 / 1.29 | 0 | no copy: shares state with other threads (compare-and-swap or memory barrier) |
| 39 | `rex_WMVideoDecDecodeP_X9_2Pass_EMB_Pass2` | 0.00 / 1.83 / 0.00 | 4 | movie / decompression code: not menu or gameplay |
| 40 | `sub_82CC1588` | 0.78 / 0.48 / 0.00 | 8 | copy built, left out: +1.8 % A57 instructions per call |

### Why 18 and not 40

sa2-nx's copies were made (its rounds 1 to 4) when its recompiled code still read and wrote guest memory as
`volatile` and kept every register in the context. Banjo's build already has what those copies gave: plain guest
memory, the non-volatile registers as locals, the context registers of call-free loops and of leaf functions with
loops as locals (`tools/loop_locals.py`, `tools/leaf_locals.py`), and a context parameter the compiler knows nothing
else points to (`__restrict`), so in straight-line code it keeps context registers in host registers by itself. What
is left for a copy to win is the hand-over at a guest call: on the Switch build no guest function is inlined into
another (the hottest 110 functions have 6,041 call sites in the generated text and 6,101 direct branches to them in the ELF of 2026-10-13), so
every call stores its arguments to the context, sets up a frame, and loads everything back.

Measured: 77 candidates (every function of the top 110 that can be a copy) were built and compared.

- **A copy of a function that calls nothing, or only large things, is not shorter.** The hook's 14 instructions and
  the loads and stores at entry and exit cost what the locals save: 40 of the 59 candidates left out.
- **The four large traversal functions get longer**, +19 % to +54 % (`sub_8294EF68`, `sub_82949790`,
  `sub_82948928`, `sub_82951B78`: ranks 1, 24, 2, 27). In the three with about forty context registers the host
  stack traffic doubles (the locals do not fit the A57's registers, and what was a context field becomes a stack
  slot); why the integer one, `sub_82948928`, grows was not looked into.
- **A copy wins where it swallows calls**: `sub_823A74E8` (one draw's state set-up) makes 16.6 calls per call as
  recompiled and 1.3 as a copy, for 26 % fewer instructions.
- **16 candidates are never comparable** (every call of them reaches a call outside the copy): not verified, so not
  in the list, whatever their numbers. Three more are only ever reached inside a caller's copy.
- **7 of the top 40 are leaves with loops**, whose registers are locals already: a copy would be the same code
  behind a hook. Among them: `sub_829A6DE8` (first on its draw thread in the console profile),
  `rex_XAudioMixFrameBufferChannel`, the memory copy `sub_82BB4510` and the shader constant writers `sub_8222EFA8` /
  `sub_8222F0D8`. Only a hand-written native can help those (see "Not done").

What the 18 are worth, from the table: together they are 13 % of the game threads' guest samples in gameplay (11 %
in the intro, 16 % in the menu), and their shares times their gains add up to about 1 % of that guest time (1.0 %,
0.9 %, 1.4 %). On the console the game threads together used 83 % of a core while the renderer thread set the frame
rate. **So no visible change is expected from this on the console; a run can only confirm that nothing got worse
and that every guard stays silent.**

**The functions that run as copies** (all keys on):

| Function | Key | Rank | PC share % (gameplay / intro / menu) | Console samples | Compared calls (verify run) | Not comparable | A57 instructions per call: original -> copy + hook | Change | Context loads + stores per call | Calls executed per call |
|---|---|---|---|---|---|---|---|---|---|---|
| `sub_823A9E80` | `banjo_cpu_native_sub_823A9E80` | 3 | 3.72 / 2.61 / 2.74 | 9 | 228,210 | 2,239,640 | 575 -> 539 + 14 | -3.8 % | 74 -> 55 | 5.7 -> 2.9 |
| `sub_823A9FF0` | `banjo_cpu_native_sub_823A9FF0` | 6 | 2.08 / 1.68 / 1.46 | 2 | 2,452,267 | 0 | 338 -> 308 + 14 | -4.7 % | 51 -> 38 | 2.0 -> 0.0 |
| `sub_82B0DCB8` | `banjo_cpu_native_sub_82B0DCB8` | 10 | 1.60 / 1.44 / 1.73 | 35 | 487,622 | 0 | 21591 -> 20548 + 14 | -4.8 % | 1414 -> 717 | 1.0 -> 0.0 |
| `sub_823A74E8` | `banjo_cpu_native_sub_823A74E8` | 22 | 1.09 / 0.72 / 0.85 | 5 | 456,905 | 472,468 | 1075 -> 780 + 14 | -26.2 % | 245 -> 17 | 16.6 -> 1.3 |
| `sub_8222AB88` | `banjo_cpu_native_sub_8222AB88` | 26 | 0.90 / 0.63 / 0.78 | 3 | 3,515,614 | 764 | 280 -> 238 + 14 | -9.9 % | 43 -> 10 | 0.0 -> 0.0 |
| `sub_8230AE18` | `banjo_cpu_native_sub_8230AE18` | 28 | 0.52 / 0.46 / 1.79 | 3 | 386,305 | 0 | 702 -> 643 + 14 | -6.4 % | 113 -> 72 | 6.0 -> 0.0 |
| `sub_823AB4A8` | `banjo_cpu_native_sub_823AB4A8` | 35 | 0.49 / 0.35 / 1.39 | 3 | 180 | 57,024 | 7047 -> 6190 + 14 | -12.0 % | 850 -> 47 | 4.9 -> 4.9 |
| `sub_823B5FD0` | `banjo_cpu_native_sub_823B5FD0` | 48 | 0.51 / 0.48 / 0.03 | 6 | 1,845,398 | 0 | 321 -> 289 + 14 | -5.6 % | 40 -> 16 | 0.0 -> 0.0 |
| `sub_823A9A58` | `banjo_cpu_native_sub_823A9A58` | 49 | 0.45 / 0.29 / 0.41 | 1 | 40,811 | 627,549 | 1502 -> 1260 + 14 | -15.2 % | 308 -> 169 | 23.8 -> 4.8 |
| `sub_8225B750` | `banjo_cpu_native_sub_8225B750` | 50 | 0.20 / 0.23 / 1.08 | 0 | 340 | 144,746 | 208 -> 176 + 14 | -8.7 % | 48 -> 34 | 4.0 -> 2.0 |
| `sub_823AC218` | `banjo_cpu_native_sub_823AC218` | 51 | 0.49 / 0.39 / 0.10 | 3 | 180,390 | 491,415 | 1989 -> 1862 + 14 | -5.7 % | 211 -> 20 | 7.7 -> 1.5 |
| `sub_8225A680` | `banjo_cpu_native_sub_8225A680` | 59 | 0.07 / 0.14 / 1.35 | 0 | 284,005 | 0 | 300 -> 263 + 14 | -7.9 % | 53 -> 37 | 3.9 -> 0.0 |
| `sub_823B5EA0` | `banjo_cpu_native_sub_823B5EA0` | 65 | 0.40 / 0.39 / 0.00 | 2 | 967,577 | 0 | 880 -> 794 + 14 | -8.2 % | 92 -> 46 | 2.0 -> 0.0 |
| XAudio SRC (mono) `rex_Process_M_03_Native_SRC...` | `banjo_cpu_native_xaudio_src_m03` | 68 | 0.24 / 0.15 / 0.64 | 0 | 1,156,324 | 0 | 3340 -> 3044 + 14 | -8.5 % | 246 -> 8 | 0.0 -> 0.0 |
| `sub_829A5DC0` | `banjo_cpu_native_sub_829A5DC0` | 71 | 0.10 / 0.34 / 0.61 | 3 | 101,570 | 0 | 8998 -> 7117 + 14 | -20.7 % | 1295 -> 479 | 95.1 -> 0.0 |
| `sub_82252C10` | `banjo_cpu_native_sub_82252C10` | 88 | 0.10 / 0.20 / 0.44 | 2 | 114,918 | 0 | 3325 -> 2684 + 14 | -18.8 % | 454 -> 30 | 20.8 -> 0.0 |
| `sub_823A9D70` | `banjo_cpu_native_sub_823A9D70` | 98 | 0.23 / 0.13 / 0.14 | 0 | 626,865 | 169,705 | 198 -> 172 + 14 | -6.0 % | 20 -> 25 | 2.4 -> 1.1 |
| `sub_8225B210` | `banjo_cpu_native_sub_8225B210` | 104 | 0.18 / 0.13 / 0.24 | 0 | 742,241 | 0 | 300 -> 263 + 14 | -7.8 % | 52 -> 37 | 3.8 -> 0.0 |

"A57 instructions per call" is an estimate, not a measurement on a console: see the next section. "Not comparable"
calls ran the recompiled original in the verify run.

**Candidates that were built, compared and left out** (figures of the candidate build of 77):

| Function | Rank | PC share % (gameplay) | Compared calls / not comparable (first verify run) | A57 instructions per call: original -> copy + hook | Why it is not in the list |
|---|---|---|---|---|---|
| `sub_8294EF68` | 1 | 5.33 | 1,800,252 / 69,218 | 1278 -> 1546 + 14 (+22.1 %) | the copy is not shorter on the A57 |
| `sub_82948928` | 2 | 4.46 | 31 / 2 | 414 -> 625 + 14 (+54.4 %) | the copy is not shorter on the A57 |
| `sub_823B59C0` | 5 | 1.68 | 1,137,243 / 0 | 936 -> 911 + 14 (-1.1 %) | the copy is not shorter on the A57 |
| `sub_8222A0A8` | 9 | 1.68 | 1,257,573 / 0 | 128 -> 123 + 14 (+6.4 %) | the copy is not shorter on the A57 |
| `sub_8222A408` | 13 | 1.50 | 2,341,959 / 0 | 88 -> 92 + 14 (+20.8 %) | the copy is not shorter on the A57 |
| `sub_82226730` | 14 | 1.61 | 983,526 / 0 | 129 -> 115 + 14 (+0.2 %) | the copy is not shorter on the A57 |
| `sub_82BDAED0` | 16 | 1.75 | 72,254 / 2 | 16729 -> 17170 + 14 (+2.7 %) | the copy is not shorter on the A57 |
| `sub_823A9C38` | 17 | 1.20 | 0 / 1,447,267 | 396 -> 386 + 14 (+1.2 %) | never comparable: every call reaches a call outside the copy |
| `sub_8237B1D8` | 20 | 1.42 | 95,601 / 242,269 | 1321 -> 1339 + 14 (+2.4 %) | the copy is not shorter on the A57 |
| `sub_8222D0E8` | 21 | 1.19 | 1,980,639 / 2,678 | 229 -> 224 + 14 (+4.1 %) | the copy is not shorter on the A57 |
| `sub_8222CB90` | 23 | 0.86 | 3,056,390 / 280 | 152 -> 149 + 14 (+7.3 %) | the copy is not shorter on the A57 |
| `sub_82949790` | 24 | 1.16 | 229,536 / 40,971 | 1191 -> 1441 + 14 (+22.1 %) | the copy is not shorter on the A57 |
| `sub_823A9B18` | 25 | 0.87 | 2,920,336 / 0 | not called through its own hook | reached only inside a caller's copy |
| `sub_82951B78` | 27 | 1.16 | 0 / 230,604 | 2022 -> 2381 + 14 (+18.5 %) | never comparable: every call reaches a call outside the copy |
| `sub_82BB4A40` | 29 | 0.56 | 2,156,846 / 0 | 670 -> 656 + 14 (+0.0 %) | the copy is not shorter on the A57 |
| `sub_8230A690` | 30 | 0.28 | 0 / 1,531 | 114676 -> 108203 + 14 (-5.6 %) | never comparable: every call reaches a call outside the copy |
| `sub_8225A188` | 32 | 0.53 | 0 / 10,090 | 8148 -> 8162 + 14 (+0.3 %) | never comparable: every call reaches a call outside the copy |
| `sub_823B49A8` | 36 | 0.47 | 1,620 / 0 | 153190 -> 153481 + 14 (+0.2 %) | the copy is not shorter on the A57 |
| `sub_82CE9250` | 37 | 0.83 | 763,219 / 0 | 319 -> 307 + 14 (+0.6 %) | the copy is not shorter on the A57 |
| `sub_82CC1588` | 40 | 0.78 | 321,536 / 0 | 571 -> 567 + 14 (+1.8 %) | the copy is not shorter on the A57 |
| `sub_8225B050` | 43 | 0.21 | 796,669 / 0 | 56 -> 54 + 14 (+21.4 %) | the copy is not shorter on the A57 |
| `sub_823AA1A0` | 44 | 0.49 | 0 / 1,447,267 | 341 -> 300 + 14 (-8.1 %) | never comparable: every call reaches a call outside the copy |
| `rex_XAUDIO__CRouterEffect__Process` | 45 | 0.48 | 0 / 1,814,116 | 3456 -> 3345 + 14 (-2.8 %) | never comparable: every call reaches a call outside the copy |
| `sub_82956770` | 53 | 0.59 | 47,155 / 43,102 | 3261 -> 3686 + 14 (+13.5 %) | the copy is not shorter on the A57 |
| `sub_82257C10` | 54 | 0.52 | 150,231 / 3 | 63 -> 75 + 14 (+41.1 %) | the copy is not shorter on the A57 |
| `rex_XUITimeline_Run` | 56 | 0.16 | 35,967 / 73,597 | 312 -> 342 + 14 (+14.0 %) | the copy is not shorter on the A57 |
| `sub_823C1080` | 61 | 0.52 | 55,168 / 0 | 1987 -> 1951 + 14 (-1.1 %) | the copy is not shorter on the A57 |
| `sub_8222F590` | 62 | 0.43 | 3,472,807 / 0 | 17 -> 21 + 14 (+104.3 %) | the copy is not shorter on the A57 |
| `sub_823159C8` | 64 | 0.40 | 0 / 328,776 | 60 -> 61 + 14 (+25.0 %) | never comparable: every call reaches a call outside the copy |
| `sub_82251F70` | 66 | 0.23 | 0 / 1,621 | 15097 -> 14330 + 14 (-5.0 %) | never comparable: every call reaches a call outside the copy |
| `sub_823A8CC0` | 67 | 0.34 | 1,385,014 / 0 | not called through its own hook | reached only inside a caller's copy |
| `sub_8223F478` | 69 | 0.27 | 0 / 1,019,957 | 354 -> 452 + 14 (+31.4 %) | never comparable: every call reaches a call outside the copy |
| `sub_8225A208` | 70 | 0.23 | 998,910 / 0 | 61 -> 63 + 14 (+26.2 %) | the copy is not shorter on the A57 |
| `rex_XAUDIO__CRoutedVoice__ProcessEffects` | 72 | 0.26 | 0 / 1,753,470 | 141 -> 145 + 14 (+13.2 %) | never comparable: every call reaches a call outside the copy |
| `sub_823AC958` | 73 | 0.35 | 227,122 / 0 | 563 -> 560 + 14 (+1.9 %) | the copy is not shorter on the A57 |
| `rex_XAUDIO__SRC__LinearVector__ProcessBlock_short_1_` | 74 | 0.32 | 102 / 0 | 8070 -> 8083 + 14 (+0.3 %) | the copy is not shorter on the A57 |
| `sub_8222D6C8` | 76 | 0.20 | 0 / 46,906 | 1264 -> 1291 + 14 (+3.2 %) | never comparable: every call reaches a call outside the copy |
| `sub_823AF198` | 77 | 0.26 | 126,476 / 40 | 388 -> 382 + 14 (+2.1 %) | the copy is not shorter on the A57 |
| `sub_823B0638` | 78 | 0.36 | 249,315 / 0 | 111 -> 99 + 14 (+1.2 %) | the copy is not shorter on the A57 |
| `sub_8224DDF0` | 80 | 0.32 | 118,024 / 123,919 | 130 -> 144 + 14 (+21.7 %) | the copy is not shorter on the A57 |
| `sub_82959230` | 81 | 0.27 | 94,426 / 181 | 1584 -> 1644 + 14 (+4.7 %) | the copy is not shorter on the A57 |
| `sub_8222A798` | 82 | 0.25 | 931,216 / 1 | 108 -> 100 + 14 (+5.5 %) | the copy is not shorter on the A57 |
| `sub_82248030` | 84 | 0.31 | 254,756 / 17,867 | 208 -> 198 + 14 (+1.8 %) | the copy is not shorter on the A57 |
| `sub_822552C8` | 85 | 0.23 | 3,960,186 / 0 | 22 -> 22 + 14 (+63.6 %) | the copy is not shorter on the A57 |
| `sub_82312DD0` | 86 | 0.27 | 105,398 / 0 | 72 -> 76 + 14 (+25.0 %) | the copy is not shorter on the A57 |
| `sub_82223270` | 87 | 0.19 | 321,127 / 43 | 160 -> 163 + 20 (+14.9 %) | the copy is not shorter on the A57 |
| `sub_8220EE38` | 89 | 0.25 | 1,488,854 / 0 | 52 -> 56 + 20 (+46.6 %) | the copy is not shorter on the A57 |
| `sub_82247BB0` | 90 | 0.27 | 334,523 / 0 | 37 -> 35 + 20 (+49.6 %) | the copy is not shorter on the A57 |
| `sub_823B0698` | 91 | 0.25 | 0 / 328,776 | 78 -> 76 + 20 (+23.1 %) | never comparable: every call reaches a call outside the copy |
| `rex_XAUDIO__CVoice__ProcessEffect` | 92 | 0.15 | 166,934 / 1,545,707 | 75 -> 81 + 20 (+34.6 %) | the copy is not shorter on the A57 |
| `sub_82248310` | 93 | 0.24 | 337,544 / 0 | not called through its own hook | reached only inside a caller's copy |
| `rex_RtlpCoalesceFreeBlocks` | 96 | 0.22 | 896,905 / 0 | 95 -> 87 + 20 (+12.2 %) | the copy is not shorter on the A57 |
| `sub_82943DB8` | 97 | 0.24 | 0 / 236,627 | 1065 -> 1174 + 20 (+12.1 %) | never comparable: every call reaches a call outside the copy |
| `sub_8224D788` | 101 | 0.23 | 382,147 / 0 | 99 -> 102 + 20 (+22.9 %) | the copy is not shorter on the A57 |
| `sub_82968310` | 102 | 0.24 | 0 / 2,988 | 16673 -> 15782 + 20 (-5.2 %) | never comparable: every call reaches a call outside the copy |
| `sub_8225F598` | 103 | 0.07 | 98,805 / 0 | 1066 -> 1100 + 20 (+5.1 %) | the copy is not shorter on the A57 |
| `sub_8224D620` | 105 | 0.20 | 409,424 / 0 | 102 -> 98 + 20 (+16.0 %) | the copy is not shorter on the A57 |
| `sub_8237DA00` | 108 | 0.25 | 0 / 67,245 | 4771 -> 5032 + 20 (+5.9 %) | never comparable: every call reaches a call outside the copy |
| `sub_8223FD48` | 110 | 0.12 | 0 / 896,006 | 358 -> 388 + 20 (+14.0 %) | never comparable: every call reaches a call outside the copy |

In that table the hook is 14 instructions, or 20 where the candidate build had more hooks than the 60 counter slots
of the context. `sub_82948928`'s count of 31 compared calls is from the first verify run, where a mistake of the
guard (one scratch buffer per thread, overwritten by the guarded calls inside a guarded call) switched it off; the
guard has a buffer per nesting depth since, and the copy was left out for its length, not for that.

**The top 60 that no copy was made of:**

| Function | Rank | PC share % (gameplay / intro / menu) | Console samples | Why |
|---|---|---|---|---|
| `sub_8222F410` | 4 | 2.21 / 1.56 / 2.54 | 8 | cannot be a copy: mid-asm hook textureTools_SetTexture_hook |
| `sub_82BB4510` | 7 | 1.29 / 2.07 / 1.83 | 7 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_829A6DE8` | 8 | 2.26 / 1.65 / 0.00 | 16 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_826567C8` | 11 | 1.72 / 1.14 / 1.62 | 10 | the renderer hooks it |
| `sub_8222BF10` | 12 | 1.62 / 1.12 / 1.25 | 10 | the renderer hooks it |
| `rex_XAudioMixFrameBufferChannel` | 15 | 1.27 / 0.84 / 2.23 | 20 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_8222EFA8` | 18 | 1.47 / 0.87 / 0.54 | 4 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_8222F0D8` | 19 | 1.10 / 0.80 / 1.35 | 5 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `rex_fast_decode_aligned_offset_block_XCOMPRESS__YAJPAUt_decoder_context_1_JH_Z` | 33 | 0.00 / 2.20 / 0.00 | 0 | movie or decompression code, not menu or gameplay |
| `sub_823B4C78` | 34 | 0.53 / 0.31 / 1.46 | 8 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_8225B330` | 38 | 0.39 / 0.36 / 1.29 | 0 | cannot be a copy: compare-and-swap, reservation |
| `sub_82364CC8` | 41 | 0.67 / 0.36 / 0.37 | 4 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_8222BDC8` | 42 | 0.51 / 0.39 / 0.51 | 1 | cannot be a copy: mid-asm hook memory barrier, memory barrier |
| `sub_8225B0C8` | 46 | 0.17 / 0.10 / 1.56 | 0 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_82BD6FD0` | 47 | 0.59 / 0.42 / 0.00 | 4 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_829A1D88` | 55 | 0.31 / 0.54 / 0.30 | 7 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_82229748` | 57 | 0.33 / 0.40 / 0.34 | 3 | cannot be a copy: mid-asm hook disable_particles_sim, mid-asm hook disable_particles_spawn |
| `sub_8224B658` | 58 | 0.43 / 0.32 / 0.20 | 3 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |
| `sub_82261560` | 60 | 0.19 / 0.22 / 0.91 | 2 | a leaf with loops: the build already keeps its registers in locals (tools/leaf_locals.py), a copy would be the same code behind a hook |

### The A57 evidence, and what it is not

Nothing can be run on the console from here, and a count of the instructions in a function says little about the
path a call takes. So (`tools/hot_arm_compare.py`):

1. a PC session with `banjo_hot_count = true` runs every copy in a counting form and writes, per function, how often
   each block of statements (a straight run between labels and jumps) was entered (`logs/hot_counts.txt`). Those
   counts do not depend on the processor: 440 s through the menus, the intro and Spiral Mountain;
2. the tool compiles the generated files that hold the originals, and `banjo_hot_guest.cpp`, with the Switch compiler
   and the Switch build's flags plus line tables (`-g1`, which does not change the code: `__imp__sub_82948928` has
   1,030 instructions in the scratch object and in the ELF);
3. every AArch64 instruction of an original and of its copy gets the count of the block its source line belongs to
   (through the lines it was inlined from), and the sums are divided by the number of calls.

What the figure leaves out or gets wrong: it counts instructions, not cycles (a load that misses the cache costs
more than forty of them); blocks that call the slow half of a float helper or the trap helper are left out on both
sides (the compiler moves them off the hot path, a source line cannot tell); the context register is followed
through the compiler's moves in address order, an approximation; and the callers of a hooked function are not looked
at. Differences of a few percent are inside its error. It was good enough to say which copies are longer.

### Verification (PC)

All runs through `tools/run_pc_test.sh` with `--masseffect_renderer_native=true` and A pressed every 6 s from second 50
on; the logs are in `app/out/pc-me`.

- **Verify run** (`--banjo_cpu_verify=true`, 1,560 s; `hot_verify_final2.1.log` and its continuation
  `hot_verify_final2.log`, the log rotates at 5 MB): title, menus, the intro cutscenes and about seven minutes of
  Spiral Mountain (verify mode runs at a few frames a second, so the same route takes four times as long). All 18
  keys ON in the `[enhancements]` line and all 18 functions reached. **13,587,542 calls compared, 0 differences**;
  4,203,311 calls not comparable (the original answered them); 2 calls of `sub_82B0DCB8` differed once and compared
  equal when repeated (the audio input changing under it, see above). The session's line:
  `[verify] checks done: ... native game functions 23012969 ... | differences: 0 (every fast path gave the same
  result as the original)`. No `DIFFERENCE` or `MISMATCH` line. The compared calls per function are the table's
  column; the two smallest are `sub_823AB4A8` (180) and `sub_8225B750` (340), whose other calls all reach a call
  outside the copy.
- **Normal run** (340 s, `hot_normal_final.log`; title to Spiral Mountain, the last 110 s in gameplay): 18 reached,
  18 on, 7,988 guarded calls compared (the first 64 of each thread, then 1 in 4,096), 0 differences, no `DIFFERENCE`
  or `MISMATCH` line.
- An earlier verify run of the same build (1,100 s, `hot_verify_final.log`) ran next to the Switch build and ended in
  the intro: 9,030,177 calls compared, 0 differences.
- **The candidate build of 77** (first verify run, 930 s, title to the intro): 42.6 million calls compared. Three
  functions reported a difference there: the two audio functions (the race, before the repeat rule existed) and
  `sub_82948928` (the guard's scratch buffer mistake described above). Both causes were in the guard, not in a copy;
  the final build's runs are the ones above.
- **Switch build**: `tools\build_switch.bat` twice (the second: nothing to do). All 18 hooks are in place in
  `app/out/switch/renut` (`sub_X` a symbol of its own, `__imp__sub_X` the original), and each original and each
  copy's entry has the size it has in the scratch objects the estimate was made from.

### On the console

Nothing to set: the keys are on in `renut.toml`. Look for `[hot] DIFFERENCE` (there must be none), for the
`[hot] <function> ON: ...` lines (each of the 18 should appear with compared calls and 0 differences; the guard also
runs the A57's own compilation of each copy against the A57's original, which no PC run can), and for
`[hot] ... is not the code the copy was made from` (a copy that stayed off). `configs/banjo_hot_functions_off.toml`
turns the 18 keys off: the game threads' shares in `rex_profile.log` of the two configs are the A/B, and with about
1 % expected it will be inside the noise of two runs.

### Not done

- **Hand-written natives.** None was written. The framework takes them (a native only has to record its stores the
  way the copies do; the original statements' recorded form and the compare of every register exist for any
  function). The candidates, by what the estimate says a call costs now: the memory fill `sub_82BB4A40` (670
  instructions a call, 1,365 call sites), the memory copy `sub_82BB4510` (2,227 call sites; its final scratch
  registers differ per path, so either all of them are reproduced or its callers' use of them is proven, as sa2-nx
  did for its CRT routines), the two shader constant writers `sub_8222EFA8` / `sub_8222F0D8` (a 16-byte copy loop
  through two unaligned vector loads, a table lookup and a byte reversal per vector), the audio sample loops
  (`sub_82B0DCB8`: 21,600 instructions a call; `rex_XAudioMixFrameBufferChannel`).
- **Flattening call chains.** The draw threads' per-draw chain (`sub_823A74E8` and the setters under it, about a
  quarter of the game threads' guest samples in gameplay) is where calls are swallowed; the closure rule stops at every
  callee that itself reaches a hooked function (the texture setter with its mid-asm hook, the draw call the renderer
  hooks). Writing the callee's statements into the caller in the tool, with one set of locals, would remove the
  hand-over there too. Not attempted.
- **The four traversal functions** (ranks 1, 2, 24, 27; 12 % of the PC's gameplay samples together and the console's
  hottest game function): a copy makes them longer. They need another idea.

### Files

`tools/localize_native.py` (the copies; `--survey <function>` says what a copy would contain),
`tools/hot_arm_compare.py`, `app/src/cpu/banjo_hot_guest.cpp`, `banjo_hot_guest.h`, `banjo_hot_common.h`,
`app/src/cpu/hot/localized.txt` (the list; one line more is one function more, after `tools/post_codegen.py` and a
build), `app/generated/hot/` (generated). Keys: the 18 `banjo_cpu_native_*`, `banjo_hot_guard_calls`,
`banjo_hot_guard_period`, `banjo_hot_count` (measurement), `banjo_pc_profile_top` (PC profiler).

## Hand-written natives (2026-10-14)

The localized copies above keep a function's own statements. These six replace the statements: the C runtime's
memcpy and memset, Direct3D's two shader float-constant writers (all four are sa2-nx's natives:
`tools/crt_signatures.py` there finds the same C runtime routines in Banjo's image, and the constant writers are the
same instructions with the same offsets) and, new here, the two functions of Direct3D's state flush that turn dirty
bits into PM4 packets. Code: `app/src/cpu/hot/banjo_hand_natives.h`; list: `app/src/cpu/hot/hand.txt`.

**How they are used.** `tools/localize_native.py` puts a hand-written native in front of the generated copy of its
function wherever that copy is used: as the function's own hook, and inlined inside every other hook's copy that
contains the function (memcpy in 2, memset in 1, the constant writers in 3). A native returns false to decline a
call it does not cover, before writing anything the original would not write first; the generated copy of the
original then runs, out of line. Each native has its own key (`banjo_hand_*`), which switches it off everywhere at
once, and each function's hook has its key as before.

**How they are checked.** By the hooks' guard, unchanged: the native is a template on the guard's store policy, so
its logged form records what it writes, and it is compared with the original's statements (every call with
`banjo_cpu_verify`). Two things had to be settled:

- *Scratch registers.* The originals leave values in caller-saved registers (memcpy in r0, r4-r12; the constant
  writers in r4-r6, r8-r11, v0 and v10-v13; ...). The natives write no register. Those registers are that
  function's skip set (the tool reads them from the original's statements; `exact=` in `hand.txt` names the ones the
  native leaves as the original does, r3 for memcpy) and the guard leaves them out of its register compare for every
  hook whose copy contains the native. Checked against the callers: sa2-nx's `tools/call_site_liveness.py` over the
  3,029 direct call sites of the six functions finds no caller that reads one of them after the call (its "r0" hits
  are r0 as the literal zero of indexed vector loads and `twi 31` traps; listed and classified). What it does show
  is callers that keep f0, f11-f13 and r7 alive across the constant writers: the game was linked with whole-program
  register allocation, so the natives must not clobber more than the originals, and they clobber nothing.
- *Stores below the stack pointer.* The guard compares every byte the original stores, and the generated code makes
  some frame stores for real: memcpy saves r3 at -8(r1), the constant writers save their (local, zero) r31 there,
  the flush writers link their frame. The natives make the same stores.

A first normal run found a hole in the guard change itself: the recompiled original that the guard runs reaches the
hand-written natives through their hooks, so it too has their registers and not the original statements'; the
compare of the original's logged form now uses the skip set too. (Verify mode could not show it: there nested hooks
leave the original's registers.)

**Verified (PC).** `--banjo_cpu_verify=true`, 340 s: 4,696,921 memcpy calls, 1,091,193 memset, 2,154,118 constant
uploads and 3,077,315 state-flush calls each compared with the original, `differences: 0`; a normal gameplay run:
0 DIFFERENCE, 24 hooks on.

**ARM first.** sa2-nx's first native Direct3D layer was correct and slower on the console than what it replaced,
for reasons no PC run showed (its `docs/api-renderer.md`, section 6). Its causes, as a checklist for this code:

| sa2-nx's cause | here |
|---|---|
| a count in the same struct as the buffer it indexes (reloaded around every store) | pointer cursors and locals only |
| byte-swapped copies word by word | nothing is byte-swapped: every copy is guest order to guest order (the originals swapped each word twice) |
| `thread_local` (a function call on the Switch) in a loop | none; the keys are four bytes read with one load; the guard's per-thread state is reached only in a guarded call |
| the whole guest context copied per helper call | no helper calls; the context is copied only in a guarded call (the first 64 of a thread, then 1 in 4,096) |
| needless copies | one copy per run, straight into the command buffer or the shadow |
| a large ring that does not fit the cache | not applicable (no new buffer) |

And the same thing happened here on a small scale, caught before any console run by counting A57 instructions
(`tools/hot_arm_compare.py` for the originals on real block counts, the Switch build's disassembly for the natives):
the first state-flush native validated every run in a pass of its own and compared exact ranges, and took 131
instructions for a call with one short run against the original's 152 on average, with five pairs of registers
saved because the generated fallback had been inlined beside it. It is now one pass with only the tests exactness
needs, and the fallbacks are out of line and cold.

| function | calls through its hook in a 340 s PC run | original, A57 instructions per call | native + hook (usual path) | |
|---|---|---|---|---|
| memcpy | 20.2 million | 239 | 68 to 84 up to 128 bytes (83 % of calls), the host's memcpy above; about 117 over the size mix | about -50 % |
| memset | 3.3 million | 669 | 48 for 4 to 7 bytes (46 % of calls), the host's memset for the large ones; about 100 over the mix | about -85 % |
| constant writer, vertex shader block | 10.1 million | 128 | 67 + 4 per vector (98 % of calls are 1 to 7 vectors): about 79 | about -38 % |
| constant writer, pixel shader block | 1.0 million | 166 | about 81 | about -51 % |
| state flush, register words | 19.1 million | 152 | 114 for one run of one word, 35 more per further run | about -25 % |
| state flush, constant blocks | 12.2 million | 228 | 151 for one run of one block | about -34 % |

The native figures are instruction counts read along the usual path, not measurements, and the sizes are from a PC
run. Together about 7 billion instructions less over those 340 s, plus the calls inlined in other hooks' copies:
roughly 2 to 3 % of the game threads' time. No frame-rate effect is expected (the game threads are not the limit).
`configs/banjo_hand_natives_off.toml` is the A/B.

**Looked at and not written by hand.**

- *The audio mix* (`rex_XAudioMixFrameBufferChannel`, 20 console samples): it is VMX code, 16 vectors per iteration;
  the recompiled form is already NEON. Nothing to gain.
- *The audio filter loop* (`sub_82B0DCB8`, the console's hottest game-thread function, 35 samples): a scalar
  recursive filter that reloads six values through byte swaps for every sample. A hand-written loop would be about
  half the instructions, but the function also has a prologue, a call and a state reset, so it needs a way to
  replace a loop inside a copy rather than a whole function. Not built.
- *Matrix and vertex-chunk natives*: sa2-nx's are for its own math library and model format; Banjo's profile has no
  such leaf above 0.5 %.
- *The Direct3D draw path as native code* (what sa2-nx calls its API renderer: DrawVertices, the state flush and
  shader binding written natively, records to the renderer instead of PM4). Not started. In Banjo that path is the
  draw entry points, the state flush (389 instructions) and its texture/sampler part (548), with draws recorded
  into per-thread command buffers that are replayed once per tile: of sa2-nx's size or larger, and sa2-nx's own
  layer ended at 8 to 9 % of its drawing thread against 12 % for what it replaced. The four writers above are the
  inner loops of that path.

## Third console run (2026-10-14) and its three crashes

Build 2026-10-14_0947, docked, logs `renut_005/006/007` of that day. Two starts at low clocks ended after 12.6 s, a
third at 2397 / 1305 / 2733 MHz ran 6.5 minutes and ended in the first level's opening scene. None of the three came
from an enhancement (no `DIFFERENCE`, no `MISMATCH`, every hook's guard at 0 differences in all three logs).

**1. Start-up, twice out of two at low clocks: a null driver in `AudioSystem::SubmitFrame`.** Banjo registers its
XAudio render driver client, and about ten seconds into start-up unregisters it and registers it again 76 ms later
from another thread (PC log: `[audio] render driver client 0 registered / unregistered / registered`, lines added
for this). The audio worker thread takes a client's callback under the global lock and runs it with the lock
released; a callback still running when the game unregistered the client then submitted its frame to a driver that
was gone. On the console's kernel that cannot happen (the unregister does not return while the callback runs); here
it was a race that the callback's length decides, and at 1020 MHz with the shader prewarm running it lost both
times. Fixed in the SDK (`sdk/src/audio/audio_system.cpp`): `UnregisterClient` waits for the client's running
callback (the lock given up meanwhile, at most 250 ms, not when the callback itself unregisters), and a frame for a
client that is not registered is dropped with a warning instead of called through a null pointer.

**2. Out of memory after 6.5 minutes: `std::bad_alloc` on the pipeline cache's writer thread.** The driver's
pipeline cache is 86 MB on the console (2,044 pipelines). The renderer kept the bytes of the file it had read for
the whole session, so that the writer thread could rewrite the file when only the prewarm list changed, and fetched
the new bytes while still holding the old: 86 MB resident and 172 MB during every save. The log's one memory line
(three minutes in) had 993 MB used of a budget of 1,279 MB with the texture cache at 342 MB of its 512 MB; the
level then filled the cache to its limit, and the next save found no 86 MB. Changed:
- the cache's bytes are fetched from the driver for each write and freed after it; nothing is kept (86 MB less
  resident, 86 MB less at every save); a save that still finds no memory is skipped with a warning (the file on
  disk stays as it is) instead of ending the process;
- `banjo_native_textures_reserve_mb` (192): with less than that left of the GPU memory budget
  (`VK_EXT_memory_budget`, asked every 32 frames) cold textures are evicted as they are near the cache's limit, and
  below half of it a batch of them. The cache's own limit counts texel bytes, which says nothing about what is left;
- the guards of the hot-function hooks give back scratch buffers over 256 KB after a guarded call (a guarded
  megabyte memset kept about four times its size for the life of its thread);
- the memory line (`C3 GPU memory`, with the pool's summary) is logged about once a minute, not once per 256 new
  textures.
Not done yet, from sa2-nx's findings of the same day in the shared renderer: eviction by descriptor slots (4,096
2D views; Banjo peaked at 929 textures in this run) and counting a texture's real footprint instead of its texels
(the pool had 142 textures on the dedicated path and 92 % fragmentation three minutes in).

**3. A pixel shader the package did not have** (`ps_5d0f87d742650991`, 15 words, loaded in the level's opening
scene; its draws were skipped). Wrapped from the console's `shader_misses` dump and added: the package has 850
shaders (was 849). The package covers the title, the menus, the intro and what the console has reported since; every
new area can have more (`shader_misses/` on the SD card lists them).

**What the run says about speed** (2397 / 1305 / 2733 MHz, docked): intro and menus 28 to 30 fps (GPU 34 ms a
Swap, 12 to 15 ms of it idle; ring thread 30 to 32 ms); the level's opening scene 22 to 27 fps, GPU-bound (38 to 56
ms a Swap, the scene pass alone 23 to 36 ms, ring thread 34 to 38 ms of work plus up to 2 s per 10 s waiting for
the GPU). The EDRAM transfers are gone from those scenes (imports 0.3 to 0.4 ms a Swap; they were 5.5). The
loading transition (the golden jigsaw pieces, and the loading screen behind them) still paid 16.2 ms a Swap for
them in that build: its depth clear was not proven as an overwrite and moved the whole depth buffer to another
representation and back every frame (2,880 tiles); see the next section. At stock clocks the same frames would
take about 2.8 times as long on the GPU and 2.3 times on the ring thread.

## After the third console run (2026-10-14): the loading screen's depth clear, deferred recording in-run

**`banjo_gpu_proof_depth_only` (non-CPU group, default on; A/B target 17).** During the jigsaw transition and the
loading screen Banjo clears the depth buffer with Direct3D's `Clear` of depth alone: a rectangle drawn through the
4x view of the buffer in the depth-only EDRAM mode (`RB_MODECONTROL` 5), with the alpha test still enabled from the
draws before it. The overwrite proof (`ProveFullOverwrite`, `me_native_system.cpp`) stopped at the alpha test
("alpha test or alpha-to-coverage can kill samples"), so the clear was drawn as an ordinary draw in the 4x view:
every tile of the depth buffer was converted into that view for it and back for the next scene draw, twice a frame
(the console's log: `depthstencil 570` rejections per 10 s, 2,880 tiles and 16.2 ms of GPU time a Swap).
In the depth-only mode no pixel shader runs (the Xenos does not run one, and this renderer builds those draws
without a fragment stage: `masseffect_native_draws.cpp`, "Mode 5 (depth only)"), and this renderer does both the
alpha test and alpha-to-mask in the fragment stage. So in that mode the two bits cannot stop a sample from being
written, and the key lets the proof go on. Nothing else about the proof changes (depth ALWAYS + write, stencil
replaced or inert, the rectangle from the CPU-run vertex shader).
- PC, the same script before and after: tiles moved by depth representation switches in the four report intervals
  around the transitions 49,272 / 13,920 / 72,416 / 134,888 -> 12,144 / 4,664 / 14,752 / 15,240; "whole-target
  runs" 49 + 16 + 80 -> 1; the `depthstencil` rejections of the "overwrite proof" line 88/44/22 -> 0.
- Picture: an in-run A/B (target 17, 2 s) recorded over the transition, and the same transition recorded with the
  key off: the same frames, including the one with black gaps between the pieces (that frame is the game's).
  `tools/ab_flicker.py` over the calm stretches: no step at the flips. 0 `DIFFERENCE`, 0 `MISMATCH`.
- The log's "overwrite proof" line now ends with how often the rule decided a draw: `depth-only mode with alpha
  bits set (banjo_gpu_proof_depth_only) N`.
- Off: `banjo_gpu_proof_depth_only = false`, or `configs/banjo_transfers_off.toml` (with the two EDRAM items of
  the round before).
What is left in those intervals (12,000 to 15,000 tiles per 10 s) are other draws of the loading screens; not
looked at yet.

**Deferred recording can be switched in the run (`banjo_ab_target = 16`).** `masseffect_native_deferred_recording`
(the Vulkan calls of the ring thread recorded by a worker on another core) stays OFF by default. futari-nx reported
that what they had seen as "not pixel-identical" was a wall-clock animation, ships it on, and measured 16-18 -> 5-6
us per draw on the console's ring thread. For Banjo:
- the picture is the same on PC: in-run A/B every 2 s over a stretch of gameplay without input, 15 flips, the
  frame difference around a flip no larger than anywhere else (2 of 15 above the 90th percentile, 1.5 expected),
  0 `DIFFERENCE`;
- on this PC it is SLOWER (ring thread 27.1 ms of work a Swap with it, 12.9 without, same run): a million calls
  queued per 10 s cost more than the calls themselves on a fast CPU with a fast driver, and 4,505 times per 10 s
  the ring has to wait for the worker to catch up (a readback, a pass that needs the queue empty).
So only the console can say. `configs/banjo_deferred_ab.toml` switches it every 4 s and prints the ring thread's
time per Swap for both from one run ("ring thread A/B" line); `configs/banjo_deferred_recording.toml` has it on for
the whole run. `masseffect::native::deferred::SetDirect()` (drains the queue, then flips the producer between
queueing and calling directly) is what makes the switch possible.

**What the ring waits for (2026-10-15).** The layer's 10 s report now names the call behind every drain ("deferred
recording drains by cause"). PC, console toml, intro route, 10 s: `vkEndCommandBuffer` 606 drains, every one waiting
for the worker, 1.74 s in all; `vkResetCommandPool` 900, 300 waiting, 0.84 s; `vkBeginCommandBuffer`,
`vkResetDescriptorPool`, `vkQueueSubmit`, `vkGetQueryPoolResults` drain without waiting (the queue is already empty).
So the ring hands the recording to the worker and then waits for it at the end of each frame's command buffer (two
a frame: the work and the output). Removing that wait means submitting from the worker too (End, Submit, fences and
pool resets drained only as far as they need), and the output: `PaintOutput` runs on the ring inside Swap and the
presenter relies on queue order after it, so the presenter's submission would have to wait for the worker's. Not done
until the console's A/B shows the layer is worth it there (`configs/banjo_deferred_ab.toml`).

**Tooling.** `tools/ab_flicker.py --log <run log>` takes the real flip times from the log's "A/B: target" lines and
the recording's start from its "[captura] recording" line (the flips drift against the 2 s grid over a run).

## Split EDRAM tiles (`banjo_gpu_edram4_split_tiles`, 2026-10-14; non-CPU group; A/B target 18)

**What was left.** After the rounds before, Nuts & Bolts' gameplay still moved 176 EDRAM tiles a frame between views
(PC, same script), 125 of them for one reason. Direct3D's `Clear` of a surface whose size is not a whole number of
tiles (the first shadow map is 1024 pixels in a pitch of 1040, the second 768 in 800, the small targets 220 in 240,
128 in 160, 180 rows in 192) clears the whole tiles with a resolve and draws the rest as rectangles: an edge strip of
tiles it covers only in part. What it leaves out of those tiles is the padding between the surface and its pitch, which
no draw through that surface ever touches; but the tile is the unit of ownership, so the renderer brought every one of
them over first from whoever held it (the scene depth in its other encoding, a colour target through the nine-pass
import), and the next surface over the same memory took them back the same way. The shadow maps' edge columns alone
were 100 tiles a frame.

**What the key does** (`masseffect_native_targets.cpp`, "banjo_gpu_edram4_split_tiles"). A draw with one proven
rectangle that bounds everything it touches, which overwrites part of a tile another view holds and touches nothing
else of it, takes the tile without a transfer. The tile is marked split: the new owner holds its part (the rectangle's
pixels), the old owner is noted as the holder of the rest, with the version it holds.
- While the owner draws inside its part (the shadow casters, the blur passes) nothing happens.
- If the tile is overwritten whole before anything needs the rest (the next frame's clears do that), the split just
  ends: the rest never moved. A whole overwrite counts only if it writes the whole EDRAM word: colour, or depth and
  stencil, or depth alone when the owner is a depth view whose stencil for the tile is all in one place.
- If anything needs the rest first (another view reads or takes the tile, the owner draws outside its part, a clear
  covers the tile in part, a stencil-only clear works in the owner), the split is healed first: the owner's parts are
  copied aside (one copy per owner), every whole tile comes over by the ordinary transfer from the view that holds the
  rest, exactly as it would have at the start, and the parts are copied back on top. The view holding a rest is kept
  like a tile owner (never parked, slept or reused).
- Two kinds: draws that write no stencil (the tile's stencil then stays where the lazy-stencil record says, for the
  whole tile) and clears that replace the stencil over the same rectangle (the part then holds depth and stencil). The
  view has to be single-sample, unscaled and able to receive copies (the heal copies the part back).
- A Direct3D depth clear of partial tiles that would be redirected into the tiles' owner is left to the drawn view
  when the drawn view itself pulled those tiles the last two times (its own draws follow; the "learned consumer").
- A colour resolve now synchronizes the pixels it copies, which is what fits the destination, not the 8-pixel
  rectangle the game sends (320x184 for a 320x180 texture); with the key off it is as before.
- A tile whose split had to be healed four times in a row is not taken split for the next 15 times (both ways
  are exact, this saves the heal's copies where the rest is read every frame: the 220x220 target is resolved into a
  224-wide texture, so its right edge column is read each frame).

**Measured (PC, the gameplay script, a 10 s report in the level, about 300 frames).**
- EDRAM tiles moved per frame: 176 -> 31. Per 10 s: 43,600 tiles taken without a transfer (2,400 of them with their
  own stencil: the 128x128 target's edge column, which came over through the nine-pass import), 42,000 splits ended
  by a whole overwrite with their rest never moved, about 1,550 healed (cleared in part in the owner 375, depth
  cleared whole with the stencil kept 320, the owner's draw or resolve outside its part 560, overwritten in part 300).
- What is left: a 4x-MSAA view's edge row (8 tiles a frame; collapsed multisample views cannot receive copies, so
  they cannot take split tiles), the tiles the learned rule leaves to the ordinary transfer (the 220x220 target's
  right column), and the occasional heal.
- 0 `DIFFERENCE`, 0 `MISMATCH`, the same 3 `REJECT` lines as without the key; `banjo_cpu_verify` run: differences 0.
- Picture, in-run A/B (key switched every 2 s, 15 flips over 30 s of gameplay without input): no step at the flips
  (largest difference around a flip 0.23 to 0.61 against 0.31 median elsewhere; the one above the 90th percentile is
  the recording's first frame). The same with `banjo_diag_split_tiles = 1` (every split healed at once) and `= 2`
  (every rest filled with magenta / depth 0.37): frames on both sides of the largest flips compared by eye, no magenta
  and no depth error anywhere; the larger differences there are Banjo's idle animation.
- Console (third run, the level's opening scene): those transfers cost 3.7 ms of GPU time a Swap at 1305 MHz (import9
  2.2, alias 0.8, import 0.4, export 0.3). How much of it goes depends on which of them were edge strips; the nine-pass
  imports there came from a 4x view's clear, which this does not cover. Not measured on a console yet.


**Second step, the same day: depth views that cannot receive copies.** The collapsed MSAA depth views are made without
TRANSFER_DST (so the driver can give them a ZCULL plane), so the heal could not copy the part back. They now take split
tiles too when the rest of the tile is one rectangle (the part spans the tile's width or height and touches a side),
and their heal imports only the rest: every pass of the depth import is clipped to that rectangle
(`edram4_import_clip_`), so the part is never touched. A depth-only record still deferred for the whole tile is
fetched first when the view uses stencil (the import would otherwise make the whole tile's stencil "real"). This is
the console's level case: the 96x96 4x-MSAA target's clear edge went through the nine-pass import every frame (16 +
16 tiles, most of the 2.2 ms of "import9" in the level's opening scene of the third run). PC gameplay: 31 -> 23 tiles
moved a frame (176 before split tiles); 16 of those are the 220x220 target's right column, which its resolve really
reads (the texture is 224 wide). Checks as before: 0 DIFFERENCE, picture A/B, the heal-at-once run (flip
differences checked by eye: Banjo's idle animation, a sparkle, grass) and the poison run (0 of 15 flips above the
90th percentile).

**Diagnostics.** `banjo_diag_split_tiles = 1` heals every split right after the draw that made it (the heal path every
time); `= 2` fills the rest of every new split tile in its owner's image with magenta or a depth of 0.37, so a read
that nothing healed would show. Both, with the key switched every 2 s (`banjo_ab_target = 18`), have to give the same
picture as the key off. The log line `EDRAM mode4 split tiles (banjo_gpu_edram4_split_tiles)` gives, per 10 s, the
tiles taken without a transfer, the splits that ended unpaid, the heals by reason, and the tiles not taken.
Off: `banjo_gpu_edram4_split_tiles = false` (also in `configs/banjo_transfers_off.toml`).

## Native Direct3D draw path (`banjo_d3d_native_draw_indexed`, `banjo_d3d_native_draw_vertices`, 2026-10-14; CPU group; A/B target 19)

Built by an agent in a copy of the tree and merged by hand; the full account (census of Direct3D's PM4 writers, the
device layout of this XDK build, the A57 estimates, the check) is `docs/d3d-native.md`. In short:
- `D3DDevice_DrawIndexedVertices` (826567C8, 79-86 % of all the PM4 the game writes) and `D3DDevice_DrawVertices`
  (8222C7A0) run as native code: their inlined state flush (dirty masks, register runs, fetch constants) and the draw
  packets, with and without the predicated-tiling wrapper. They write THE SAME PM4, byte for byte, into Direct3D's own
  command buffer and leave the device as Direct3D does: Banjo's engine records draws on worker threads into command
  buffers it replays once per tile and patches after the call, so records (sa2-nx's way) would have broken that, and
  measured in this tree they made the ring 4 % slower. Direct3D's own code still does the draw-time shader load and
  two rare blocks (called in place with the same registers); a call it cannot do plainly (over 65535 vertices, no room
  in the command buffer segment) goes to the original.
- The check: on the first 64 calls of each thread, then 1 in 4096, and on every call with `banjo_cpu_verify`, the
  native code and the original each run on a private copy of the device into the same scratch command buffer and are
  compared byte for byte (packets, the whole device, the tile-extents entry); then the original runs for real. One
  checked call in eight sends every block through Direct3D's own writers. A difference switches the entry point off.
- Merged tree, PC: `banjo_cpu_verify` run 649,563 DrawIndexedVertices and 18,545 DrawVertices calls compared, 0
  differences; 80 s smoke run 960,000 native calls, 0 differences.
- Speed: game (drawing) threads only, about 19 % less per DrawIndexedVertices call on the PC (in-run A/B); on the A57
  about 865 -> 500 instructions for the replaced part (estimate from the compiler's output), shader load excluded. The
  ring thread is unchanged (13.8 against 13.9 ms of work a Swap): no frame-rate change expected.
- Next stage (not done): the native shader load (8222BF10, about 900 of the 1,400 A57 instructions a call takes now),
  BeginVertices / DrawVerticesUP, Clear, Resolve.
Off: both keys `false` (`configs/banjo_d3d_native_off.toml`).

## Early alpha test (`banjo_gpu_early_alpha`, 2026-10-15; non-CPU group; A/B target 26)

The heaviest scene shaders of the intro cutscene on the console are one family: Nuts & Bolts' shadow-receiving
materials (PS536, PS447, PS555, PS765 and the in-place PS167, PS201, PS252, PS159, PS112, PS259, PS656, PS37; about
30 ms of GPU a frame at stock clocks). Each runs three shadow cascades of 8 taps and a light loop, then an alpha test
at its very end (`banjo_diag_ps_alpha`: 255,264 of 277,380 PS536 draws in 300 s test alpha > 16/255). Their alpha
needs only the first one or two fetches: the shadow sum is added to it times c12.x (PS536 family; 0 at run time in
those draws) or times a literal 0 of the shader's own constants (the in-place family).

`tools/early_alpha.py` (a step of `tools/build_shader_library.sh`, between XenosRecomp and DXC) works on the HLSL of
every pixel shader: it slices main() backwards from the alpha clips (per component), finds the earliest top-level
point after which that slice has nothing it cannot repeat, and emits `banjoEarlyAlphaFails(<registers live there>)`,
a copy of the slice, plus a call at that point under `SPEC_CONSTANT_EARLY_ALPHA` (bit 23) that discards a 2x2 quad
whose four invocations all fail (`banjoEarlyAlphaQuad`: fine derivatives of a 0/1 value). The renderer sets the bit
with the alpha test (functions other than != and always) for shaders whose SPIR-V has it, so other pipelines keep
their keys.

Why it is exact:
- The copy is the same operations on the same values, only the components the alpha needs. A product of a run-time
  constant and a finite value that depends on fetches is 0 when the constant is 0: the copy uses 0 under the guard
  `constant == 0 && |constants the bound uses| <= 2^40`, tested per draw; a literal 0 needs no guard. A zeroed value
  may reach the alpha only through operations where +0 and -0 agree.
- A fetch is repeated in the copy only where every if around it tests uniform values and no lane can have left
  before it: all four lanes of every quad sample there, as at the early point, so derivatives and texels match.
- `banjoAlphaFailsEarly` fails a pixel only when the test fails for every alpha within (|alpha| + |ref|) * 2^-12 + 1e-30
  of the copy's value (a multiply-add fused in one place and not the other, a flushed denormal).
- Whole quads only: a pixel that is still shaded never loses a neighbour its implicit-LOD fetches need.
- Self-check at build time: every copy is run against the original main() by an interpreter for the same statement
  forms (float32) on 24 random sets of registers, fetch results, constants and bool constants per shader; wherever the
  guards hold and the original reaches an alpha clip, the copy must give the bit-identical alpha. A shader whose copy
  differs once keeps none (`out/shaders/early_alpha.txt` lists every shader and why). A run of the strip-and-redo
  path on an already processed folder gives byte-identical files.

Library of 2026-10-15: 464 of 685 pixel shaders have a copy (167 have too little after their alpha to gain, 54 use
forms the tool does not read: depth output, loops, address-register constants, a few named samplers); the whole
heavy family skips 26-30 of its fetches in a failing quad.

Measured on PC (2026-10-15, console toml, intro route, 300 s each, `banjo_diag_gpu_groups = 2`, key on against off):
the alpha-tested groups of the family lose 9 % (PS536, alpha test + blend + depth write) and 10 % (PS447) of their
GPU time, the small ones 30-54 % (PS765, PS555); shaders it cannot touch move by +-4 % between the two runs (noise).
Many of the family's draws on this route are not alpha tested (state code 60012: depth write and test only), so the
family as a whole goes 0.74 -> 0.73 ms a Swap on the PC's GPU. The fraction of quads that fail is the limit, not the
copy. In-run A/B (target 26, 4 s halves) and a recorded A/B (2 s halves, `tools/ab_flicker.py`): no picture change
at the flips (in the still part of the cutscene the frame differences at the flips are those of any other frame);
both runs 0 differences. The console run will say what it is worth there (PS536 alone is 12 ms a frame at stock).

## PMULL fingerprint (`banjo_ring_pmull_fingerprint`, 2026-10-15; CPU group; ARM only, no in-run A/B)

The console's ring thread (sixteenth package, intro cutscene, stock clocks, `tools/switch_hot_symbols.py` on
rex_profile.log) spent 8.8 % of its samples in `RangeFingerprint`: the persistent vertex arena hashes every window it
cannot prove by interval or submission, about 1.35 GB every 10 s, plus XXH3 (2.7 % + 0.6 %) for the texture rechecks.
The CRC32 pair of `masseffect_crc_fingerprint.h` runs four CRC instructions per 16 bytes, all in the A57's single M
pipeline: 4 bytes a cycle at best (the console reached about 1.5).

`banjo_pmull_fingerprint.h` folds a CRC-64 (ECMA-182 polynomial) with PMULL / PMULL2 instead: four independent chains
fold every fourth 16-byte block by x^512, are combined, the tail is folded block by block, and a Barrett reduction
gives the exact residue; the seed is added to the first block and the length enters a bijective final mix. Its main
loop (checked in the Switch object file) is 8 PMULL, 2 LDP and 8 EOR per 64 bytes, about 8 cycles against the CRC32
pair's 16. Guarantees as before: a change spanning fewer than 64 bits of a block is always seen, other changes escape
with probability 2^-64.

Checks: `sdk/tests/pmull_fingerprint_test.cpp` (`tools/run_sdk_tests.bat pmull`), through simde's model of the same
intrinsics: 2,811 lengths and seeds against the bit-by-bit reference, 0 differences; every one of 28,192 single- and
short two-bit changes of a 1 KB message seen. On the console the first 256 calls and then 1 in 65536 (1 in 64 with
`banjo_cpu_verify`) are recomputed by the reference; a difference writes DIFFERENCE and returns to the CRC32 pair for
the session (stored fingerprints then simply stop matching: the windows are read again, never trusted wrongly). The
log line `PMULL fingerprints (banjo_ring_pmull_fingerprint on)` counts the calls and checks. Off:
`configs/banjo_pmull_off.toml` (fingerprints are compared across draws, so the key cannot switch inside a run).


## Guest register writes nothing reads (`tools/dead_ctx_defs.py`, 2026-10-15; build-time, `BANJO_GEN_DEAD_DEFS`)

Found while looking at the hottest game function on the console's game threads, the bytecode interpreter
`sub_82948928` (9.8 % of its thread, the one whose localized copy came out 54 % longer): in a function that calls
anything, the volatile registers live in the PPCContext, and every guest instruction that sets one stores it there.
The compiler keeps those stores (a call may read ctx; every guest load goes through a pointer it cannot separate from
ctx), so a write the guest program itself never reads still costs a store, and its computation. The interpreter's
dispatch, for example, loads the jump table entry into r0 and the table address into r12 for a `mtctr; bctr` the
generated `switch` no longer needs: three context stores and a guest load per opcode.

The step reuses `tools/leaf_dead_stores.py`'s analysis of the guest program (per function, what its callees read
before writing, UE, and what its callers read after it returns, LiveOut, to fixed points over the call graph; nothing
assumed from the calling convention between direct calls, since the 360 compiler allocates registers across them). Per
function, a backward liveness over the guest instructions; a single-register write live nowhere after it is dead, and
removing it removes its reads, to a fixed point. Its statement becomes `REX_DEAD_DEF(reg, statement)` (nothing), or,
for a record form (`clrlwi.`, `rlwinm.`, `and.`: a bit test into a scratch register, the commonest case),
`REX_DEAD_REC(...)`, which computes the value into a temporary for cr0 only. Additions to the analysis:
- the generated `switch (ctx.rN.u32)` of a jump table reads rN (the guest `bctr` reads only ctr), and the `mtctr` before
  it reads nothing (ctr is never read after it);
- with `--abi-indirect` (on): a function called through a pointer is read by its caller only in the calling
  convention's terms (non-volatile registers, r1, r2, r13, the return registers r3, r4, f1-f4), and a call through a
  pointer reads everything but the scratch registers r0, r11, r12 and f0: a caller that does not know its callee cannot
  pass anything else (the exceptions the earlier audit found, v64-v77 and v92, are vector registers, outside this
  analysis).

First counts on Banjo's tree: 25,672 plain writes and 13,952 record forms left out, in 10,537 functions (without
`--abi-indirect`: 19,475 and 12,569 in 7,439). A re-run restores its statements first, so it is idempotent (two runs
give byte-identical files).

**Before it went in (2026-10-15 morning).** The PC build with it crashed at start. Two faults
were found and fixed, and both were in the analysis it shares with `leaf_dead_stores.py` and `single_floats.py`
(`docs/codegen-enhancements.md`, "The shared analysis"):
- the cascade removed the input of a record form (an `lbz` feeding `rlwinm.`), whose cr0 result is read: a record
  form now keeps its reads;
- a `goto` to one of `loop_locals.py`'s border stubs was taken for an edge to the instruction after the stubs.
  `parse_function` now follows it to the stub's target. This one also changed `single_floats.py` (31 instructions
  fewer proven single) and `leaf_dead_stores.py`, which were built with the wrong graph since the sixteenth package:
  the next build has the fix.
- Also new: the functions the generator names (`rex_*`) are read from its function table and their calls resolved,
  instead of counting as reading everything.

With both fixes the build still crashed at start (a write to guest address 4 on the main thread). Bisected by file
(`tools/dead_defs_bisect.py`: 213 files -> 54 -> 27 -> 13 -> 6 -> 3, the last step down to `renut_recomp.120.cpp`)
and then read: the **mid-asm hooks**. The generator calls a hook's host function inside the guest code, with its
registers by reference (`textureTools_SetTexture_hook(ctx.r5);`) or as a test that may jump
(`if (disable_particles_draw0()) { goto loc_X; } else { }` and then the guest call). The parser takes registers from
the instruction comments and took the hook's goto for an unconditional end of the instruction, so it never saw the
call after it: in the particle draw `sub_8222ADA0` the `this` pointer (r3) and r4 = 1 of the four calls behind
`disable_particles_draw0`-`3` were left out. The parser now counts a hook's arguments as reads and its test's
gotos and returns as branches it may take (the 40 hooks are the generator's `extern` declarations). That fix moves
nothing in `leaf_dead_stores.py` and one `fmuls` in `single_floats.py` (no longer proven single).

**Status (2026-10-15, eighteenth package): on.** PC, the console's `renut.toml`, the 300 s intro route: the normal
build and the poison build both with no crash, 0 `DIFFERENCE`, recordings without anything broken. 18,754 plain
writes and 15,340 record forms left out, in 13,846 functions. Not yet run on a console, and the PC route does not reach
the other levels.

Switches: `BANJO_DEAD_DEFS_OFF=1` (environment of a build) takes the rewrites back out; `-DBANJO_GEN_DEAD_DEFS=OFF`
compiles the statements again (`REX_DEAD_DEFS_KEEP`); `-DBANJO_GEN_DEAD_DEFS_POISON=ON` writes 0xDEADBEEF... into each
left-out register, so a reader the analysis missed breaks the game visibly (the PC test, never shipped).

## Package 26 (2026-10-16/17): the shader round, the partial stencil fetch, and Renut's renderer

**banjo-nx's renderer.** Two keys, both exact, with in-run A/B: `banjo_gpu_literals_cbuf` (A/B 56: the literal
constants NAK cannot encode as immediates read from the constant registers Direct3D loaded them into) and
`banjo_gpu_stencil_clear_partial` (A/B 57: a stencil-replacing Clear fetches only the tiles it covers in part, the
once-a-frame `edram_import9` of the levels). `banjo_diag_import9_steps` times a 9-pass import step by step on the
console. Details: `docs/enhancements-reference.md`, "Package 26"; `docs/shader.md`, "Package 26".

**Renut's renderer** (`banjo_renderer = "renut"`), the user's overnight mandate of 2026-10-16: Renut upstream's
Direct3D-level renderer built into the same NRO, on the Switch, and tuned for the A57 and NVK with banjo-nx's own
measured items: constants through uniform buffers, branchless select, clamp bounds from the constant bank and
predicated blocks without a branch in Renut's translator; thread placement (`banjo_ring_core`, `banjo_background_compile_core`, `banjo_prewarm_threads`);
`banjo_renut_pipeline_cache`, `banjo_renut_precise_barriers`, `banjo_renut_upload_cached`, `banjo_renut_pmull_hash`;
the `[renut]` 10 s report and its in-run A/B (`banjo_renut_ab_target`). The codegen improvements and the CPU group's
game-code items are the game's and run with either renderer. Everything about it: `docs/renut-renderer-port.md`.

## Package 27 (2026-10-17): the rest of Renut's renderer, and a codegen round

The user's mandate: port to Renut's renderer every banjo-nx item it did not have yet (16 shader translator keys and
the NVK draw contract), make it safe on the console's memory (no texture eviction glitches like sa2-nx's), and a round
of 15 aggressive codegen ideas, audited, the ones that hold implemented.

**Renut's renderer** now reads `banjo_gpu_nvk_draw_contract` (and NVK's five part keys), `banjo_gpu_robustness_off`,
`banjo_gpu_bool_spec`, `banjo_gpu_blend_skip` and `banjo_gpu_tess_grid` (its in-run A/B targets 7-9 for the last three);
its translator has banjo-nx's literals from the constant bank, bone rows in one load, bone fetches without their branch,
the moves as their operand and early alpha (3,663 of 3,936 pixel shaders). The other eleven translator keys have
nothing to do there (each checked over the whole library: no `switch (pc)` loops, no texcoord swap, no texture sign
codes, the input assembler remaps, linked stages). Exactness and memory, not enhancements: `banjo_renut_exact_streams`
(cached vertex and index copies hashed at their first use after each kick), `banjo_renut_memory_policy` (console-sized
limits, cold textures only, never one the GPU wrote, the driver's real budget through `VK_EXT_memory_budget`).
Details and counts: `docs/renut-renderer-port.md`, section 7; the keys: `docs/enhancements-reference.md`, "Package 27".

**The codegen round** (both renderers: it is the game's code): VMX vsel as one NEON bit select, the console profile's
functions gathered by the linker (their 4 KB pages 1,449 -> 584), inline copies of tiny leaf functions in their
callers' files; twelve ideas audited out (two localized copies that measured dearer or that their guard could not
check; `-mno-outline-atomics`, which changed nothing: GCC did not outline our atomics). Each with its CMake switch:
`docs/codegen-enhancements.md`, "Package 27's codegen round".

## Bulk localized copies and direct dispatch (2026-10-22, bejeweled3-nx's method)

The user's request: apply the Bejeweled 3 port's CPU method (its `docs/optimising-functions.md`: four steps that took
its heaviest frames from 25 to 15 ms on the console) to Banjo, from the console profiles we already have, because the
busy town is starting to be CPU-limited. Its four steps, and where Banjo stands on each:

1. **Changed-only register write-backs in every copy.** Already Banjo's: `tools/localize_native.py`'s `dirty_sets` is a
   may-differ dataflow over each copy (a return stores only the registers changed on some path to it; a call stores
   only those changed AND reachable by its callee, and loads back only those the callee can change; a `switch` jump
   table is followed, not given up on). Nothing to port.
2. **Bulk localization.** New. `tools/switch_hot_bulk.py` reads console profiles (`rex_profile.log` with the ELF of the
   NRO that wrote it), adds up each recompiled function's samples over every thread, and writes
   `app/src/cpu/hot/localized_bulk.txt`: every function still in generated code with at least 8 samples over the
   seven console runs of 2026-10-21 (packages 36u to 36z3, gameplay reports only), except tiny leaves (12 guest
   instructions or fewer, no call: the generator inlines them already), forms the copy tool refuses (time base reads,
   atomics, host calls), anything an app or SDK source names (their own hooks), and the five script interpreters whose
   jump tables `tools/switch_goto.py` threads (a copy is made before that step and would undo it). 363 functions;
   `tools/localize_native.py --list --bulk` (run by `tools/post_codegen.py`) made 360 of them (three have a callee with a
   switch form it does not know). They cover 18,982 of the 20,887 samples a threshold of 3 would take (808
   functions). Their hooks (`BANJO_HOT_HOOK_BULK`) go into `app/generated/hot/banjo_hot_bulk_<k>.inc`, twelve shards by
   header size, each compiled by its own `app/src/cpu/banjo_hot_shard_<k>.cpp` (`banjo_hot_prelude.h`; CMake's
   `BANJO_HOT_SOURCES` gives them the copies' compile rules). One master key, `banjo_cpu_native_bulk`; each copy also
   has its own `banjo_cpu_native_sub_<address>` (category "Banjo/Bulk copies"). The `[hot]` report summarises them
   ("bulk copies ... N of 360 reached, M on") and names one only when its guard found a difference.
3. **Leaf callees inlined in the copies.** Already Banjo's, in a stronger form: a copy brings its direct callees along
   as copies of their own (the closure: any callee whose whole call tree can be copied, up to 1,200 lines a member and
   6,000 a copy, inlined by force up to 300 lines or when called once), where bejeweled3-nx inlines leaves of up to 12
   (or, for its hottest, 160) instructions. The bulk copies make 2,273 calls to inlined callee copies and 2,068 real
   calls (indirect, imports, app hooks, call trees too large).
4. **Direct hook dispatch.** New. With `banjo_hot_guard_calls = 0`, `banjo_hot_guard_period = 0` (now allowed: "no
   guard"), verify and `banjo_hot_count` off, a hook's state is `kDirect`: one byte and the copy, no countdown to the
   next checked call (for the bulk hooks, past the 60 per-thread slots, that countdown was a counter shared by every
   thread and written at every call). `renut.toml` ships this; `configs/banjo_hot_guard_on.toml` puts the guard back
   (64 first calls per thread, then 1 in 4096) and `configs/banjo_cpu_verify.toml` checks every call. The Switch build
   no longer makes the counting form of the copies (a PC measurement). The report now comes with the renderer's 10 s
   line (`banjo_renut_tuning.cpp`): without a guard nothing else calls it.

The A/B of the round: `configs/banjo_cpu_native_bulk_off.toml` (the 360 back to generated code, the hand-picked 25
stay). Earlier finding to watch (this section's "Why 18 and not 40"): copies of functions with about forty context
registers measured longer on the A57; the bulk copies have a median of 14, one has 41 (`sub_82226BD0`).

**A guard bug the bulk copies found** (emulator, guard on: `banjo_hot_guard_calls = 64`, `banjo_hot_guard_period =
4096`). One repeatable difference in 329 reached copies, `sub_82226330` (a draw's vertex-shader constants: a 4x4
matrix built by its callee `sub_8225F3F8`, uploaded by Direct3D's constant writer): the copy and the original wrote
different constants. Not the hand-written constant writer (the same with `banjo_hand_constants = false`): the matrix
is written with unaligned vector stores, which `tools/vec_unaligned_stores.py` writes as `REX_STVLX` / `REX_STVRX`
(one helper call), and the copies' logged forms did not record that form (only `REX_STORE_*`, raw vector stores and
dcbz). The guard neither rolled those stores back nor compared them, so a guarded call made them four times. The
copy's play form was exact all along (the same helper as the generated code). `localize_native.py` now records them
in the logged forms (`if (s.Note(<the bytes they cover>)) REX_STVLX(...)`; the play form's `Note` is a constant true)
and refuses any other form of them; 33 copy headers had them, hand-picked ones included. For the other ports with this
tool (sa2-nx's family): the same gap is there wherever the copies are made after `vec_unaligned_stores.py`.

## Render thread night and the shadow sort (2026-10-27)

The record of the work and its measurements is `docs/record-time-draw-path.md` (sections "Render thread night", "Render
thread audit", "Shadow sort" and "Console run of the shadowsort package"). The console's city run of
builds/2026-10-27_1047_shadowsort (handheld, stock clocks): 17.2 fps, render thread 53.6 ms, GPU 40.5 ms, 10.28 us of
render thread a draw (setters2: 16.1 fps, 56.9 ms, 41.0 ms, 11.23 us). Its in-run A/B over targets 41-26 put every item
within -3.7 % ... +4.0 %; two cost time and are now OFF by default, each with a note at its definition: the shadow sort
(`banjo_renut_shadow_sort`, A/B 40: render thread +1.13 ms, GPU +0.65 ms) and the replay's look-ahead prefetch (its own
key now, `banjo_renut_replay_lookahead`, A/B 29: +2.15 ms in one period; OFF, SLOWER ON THE CONSOLE, confirmed at
+2.79 ms by night_gpu5's 30 s check). Found the same day: the A/B target list kept
only targets below 32 and the single target was limited to 0-19, so the night packages' lists never ran 32-39; both
now go to 63.

## Four cores, mesh objects, XMA cache, record-time extents, shadow culling (2026-10-27 afternoon)

The user's five-hour block (CPU-gap items 8, 2, 3, 4 and NetherSX2_nx's 4-core mode), then "more like Unleashed".
Emulator title-screen runs only (b26-b32); console numbers pending.

- **Four-core mode** (`banjo_system_core`, `banjo_system_core_compiles`, `banjo_forwarder_program_id`; NetherSX2_nx,
  MIT, NaGaa95): `docs/four-core-mode.md`. Banjo writes its forwarder's four-core NPDM itself
  (`sdmc:/atmosphere/contents/05D6EA1333487000/exefs/main.npdm`). Thread placement only.
- **Mesh objects** (#8, geometry side; `banjo_renut_mesh_objects`, A/B 42): about 70 % of the whole-buffer draws
  answered by one probe on the emulator's title screen; verify 0 differences.
- **XMA frame cache** (#4; `banjo_xma_frame_cache`): 10-30 % of the decoded frames answered on the title screen;
  catch-up checks 0 differences.
- **Record-time extents** (`banjo_renut_record_extents`, A/B 43): the texture export checks from extents the recording
  threads work out; 88 % of the checks; verify 0 differences in about 3 million.
- **Shadow-caster culling** (`banjo_renut_shadow_cull_indices`, **off**; not pixel-identical): `renut_shadowcull.toml`.
- **Not built**: #2 (the voice filter across voices: XAudio mixes each voice's filter output right after it, one voice
  at a time), #3 (the reverb is already NEON), the texture side of #8, the replay without the device image (it would
  cost more than the chunk copies it saves). Reasons in `docs/record-time-draw-path.md`.

## Phase A, the replay stream, fetch cache and geometry objects (2026-10-27 evening)

- **Phase A** (`banjo_renut_resource_objects`, A/B 45): texture objects resolved by the recording threads. Console A/B
  45: -2.26 ms (-4.3 %), 4 of 4 pairs. Emulator verify 27.4 million answers, 0 differ.
- **Replay stream** (`banjo_renut_replay_stream`, A/B 46): two-line, 64-aligned ops and a three-stage prefetch.
- **Fetch cache** (`banjo_renut_fetch_cache`, A/B 47): vertex fetch data kept across frames, proven by the store cards;
  emulator ring copies 240 -> 5-16 MB per 10 s.
- **Geometry objects** (`banjo_renut_geometry_objects`, A/B 48): stream and index buffer objects resolved at record
  time; replace the mesh objects.
- Constants and deko3d recording: data the push and the pipeline bind read moved into lines already in cache, and
  prefetched. Details: `docs/record-time-draw-path.md`; for other ports: `docs/porting-guide-renderer.md`.
- **Not built**: Phase B (loader hooks), see the porting guide's section 1b.

## Total CPU (2026-10-28)

The c4 console run (four cores, static city, 19.6 fps): the three game cores about 98 % busy (render thread 47 ms,
main thread 28, the two recording threads 47 of which about 20 ms is the recording code, others 25). The frame follows
the total CPU on those cores, so the user chose total-CPU cuts over moving draw translation to the recording threads.

- **Recording lookups** (`banjo_renut_record_lookups`, A/B 50): keys in the object tables (one line a lookup), the op's
  lookups batched so their misses overlap, a per-device memo of the last objects. Exact.
- **Fast pipeline entry**: the pipeline and its check on the first line, the key's draw-time fields on the second.
- **Background compiles**: share 10 % (was 25 %), rest up to 30 s.
- **A/B 49** (the replay stream's shader stages) off by default: +0.7 ms on the console.
- **A/B halves** now report the CPU of all threads a frame (Switch).
