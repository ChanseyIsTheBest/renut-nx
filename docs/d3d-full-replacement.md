# Direct3D, fully replaced (plan and progress, from 2026-10-16)

The user's order (2026-10-16): replace the game's Direct3D at the API level completely, the way UnleashedRecomp does,
including the command-buffer recording, "no half measures", because it is the frame-rate limit here and because every
ReXGlue port of an Xbox 360 game can reuse it. This page is the plan, the facts it rests on, and the record of each
step. [d3d-native.md](d3d-native.md) is what came before (the native draw path that still writes PM4, the Clear and
Resolve operations).

## Why

The console's frame rate is set by the ring thread (package 21b, diagnostics run of 2026-10-16, title screen, stock
clocks): 99.3 % of one thread, 69 ms of work per 72 ms frame, 26.4 us a draw for about 2,600 draws. The GPU is busy 44
ms of that frame and idle for the rest, waiting for the ring. The ring's work per draw (console, `C6 substages`):

| Part | us a draw |
|---|---|
| PM4 parsing and identification (decoding registers, finding shaders and textures from raw words) | 5.35 |
| Vulkan driver calls (pipeline bind 3.49, draw 2.92, UBO set 1.61, buffer binds 0.34) | ~8.4 |
| Index stage (conversion, upload or the persistent arena's proof) | 3.67 |
| Texture and sampler stage (fetch constants to descriptors) | 2.94 |
| Pipeline lookup, shared constants, targets, vertices | ~3.2 |

UnleashedRecomp has no ring: its Direct3D functions are native and hand a render thread commands that already name
their objects. Banjo's Direct3D is the game's own recompiled code: it writes Xenos PM4, and the ring works every draw
back out of it.

## What Banjo's Direct3D is (facts)

- **Most setters are inlined into the game.** This XDK's `SetTexture`, `SetVertexShaderConstantF`, render states and
  the like are inline code that writes the device structure's shadows and dirty masks (24,064 bytes; layout in
  d3d-native.md section 2). They are not functions a hook can take. What a replacement reads is the device at draw
  time, as `banjo_d3d_native.h` already does: the dirty masks, the shadows, the bound shader objects.
- **Real functions:** `D3DDevice_DrawIndexedVertices` (826567C8, 79-86 % of all PM4), `DrawVertices` (8222C7A0),
  `DrawVerticesUP` (8222E520) and `BeginVertices` (8222CC50), the standalone FlushState (82222888), `Resolve`
  (8222D6C8), `Clear` (822812C8, one rectangle at 82223560), `BeginTiling` / `EndTiling` (822788A0 / 82212328) and
  the per-frame tiling functions (8227A9F0, 8227B218, 82278C08), `SetShaderGPRAllocation` (82220B20), `Swap`
  (82287590), `BlockUntilIdle` (82657488), the kick (`82210408`: one INDIRECT_BUFFER per segment into the primary ring,
  then CP_RB_WPTR), the space functions (82210AB0, 821E2940, 821F7C18, 8222D240), and the 73 functions
  `tools/d3d/d3d_writers.py` finds writing after the device's write pointer.
- **Recording devices.** The engine records its draws on two worker threads, each with its own device object
  (CreateDevice type 2: flag 0x80 at +10940), into command segments of their own. A recorded command buffer object
  holds its segment list at +116 (next block at +0, count at +4, entries of address and length from +8). The main
  device runs one by its FlushState writing one INDIRECT_BUFFER per segment (82222888's loop at 82222D20; header
  0xC0013F00 built at 82222D20-82222D2C), once per EDRAM tile under predicated tiling.
- **The frame's stream** (PC trace of 2026-10-16, intro): the primary ring holds 6-8 main-device segments a frame;
  inside them about 10 recorded command buffers (up to 8,160 words each, 40-166 draws each, chained segments of one
  recording) and Direct3D's own small fixed buffers (`1FB00000` 74 words, `1FB01980` 24 draws, run before each
  recorded buffer: per-tile set-up and clears).

## The design

Three kinds of threads, as in UnleashedRecomp:

1. **The game's threads call native Direct3D.** Every function that writes PM4 today is native code that appends host
   records to the command list of its device instead (the main device's list, or the recording device's list for the
   command buffer being recorded). A record names objects (the shader pair, texture objects with their fetch-constant
   words, the vertex and index buffers by address and size, the render-state words, the constants by value as
   Direct3D copies them into the command buffer today) and is prepared on that thread as far as the inputs allow:
   pipeline key, shader identity, sampler state. The recording threads are 12-16 % busy today; the work moves to
   them from the one thread that is at 100 %.
2. **One executor thread** (the ring thread's successor) walks the main device's list in submission order: it runs
   recorded command lists where the main device runs a command buffer, does what must happen at execution time
   (vertex and index uploads from guest memory, which can change between recording and running; resolves; the
   texture cache), and hands the render thread prepared draws.
3. **The render thread** turns prepared draws into Vulkan calls and submits (the deferred layer's worker, with draw
   packets since 2026-10-16), so the driver's 8.4 us a draw is not on the executor.

What stays exact, and how each step is checked:

- During the change, a native function writes its record AND the PM4 it replaces; in verify mode the ring decodes the
  PM4 and compares it with the record (the method of `banjo_direct_state` and of the native draw path's two-copy
  check). A step is switched on only when its records match the PM4 for a whole verify route, with a key and an in-run
  A/B per step.
- Execution-time inputs stay at execution time: vertex and index data and textures are read from guest memory when the
  executor reaches the draw, as the GPU did (a recorded command buffer runs again later with new bone data).
- Direct3D's own bookkeeping the game reads (the device's write pointer, segment limits, fences at +10896 / +10908,
  the tile extents, the vertical blank counters) is kept as Direct3D keeps it until a later step replaces it with host
  fences.

## Steps

| # | Step | Removes from the ring | State |
|---|---|---|---|
| 0 | Ring thread pinned to its own core (`banjo_ring_core`); deferred recording's worker pinned (`banjo_deferred_core`) and fed with draw packets (`banjo_deferred_packets`) | the driver calls (8.4 us a draw) when deferred recording is on | **built, console A/B pending** |
| 1 | Census of every Direct3D function that writes PM4, per frame and per device (`tools/d3d/d3d_census_build.py`, with the 73 writers): the complete list the replacement must cover | | **done** (below) |
| 2 | The ring prover (`banjo_ring_prover`): the execution-time content checks (fingerprints of the persistent vertex and index windows) read on another core as soon as the game submits, before the ring reaches the draw | the fingerprints (10.9 % of the ring's samples) | **removed 2026-10-16** (console: no gain, 41-43 % of a core; "The prover removed") |
| 2b | Shader loads proven by the prover (`banjo_ring_prover_loads`): the prover fingerprints every cleared IM_LOAD's program; the ring compares fingerprints instead of program words | the microcode compares (`memcmp`, the load memo: ~3.5 %) | **removed 2026-10-16** (console: no gain, 41-43 % of a core; "The prover removed") |
| 3 | The ring split (`banjo_ring_decoder`): PM4 decode, shader identification, the tiles bookkeeping and WAIT_REG_MEM on a decoder thread, which hands the ring (now the executor) the draws with the register pages written since the item before; fences, interrupts and memory writes stay the executor's, in stream order | Type3 / Packet / WriteRegister / identity (~7-10 %) | **built** |
| 4 | Every other PM4 writer native (DrawVertices / UP / BeginVertices, Clear, Resolve, tiling, GPR allocation, the fixed buffers): records for the decoder instead of PM4 | the decoder's PM4 (off the ring by then) | **built, on** (`banjo_d3d_records`: records of whole calls for all 69 writers, the barriers and Direct3D's fixed buffers included; see "Step 4" and "Step 6b"); package 24 |
| 5 | Recorded command buffers as host command lists: the recording devices' native draws append records to the command buffer's list; running a command buffer runs its list | segment parsing on the decoder | **built, on**: the records work with the split; the native draw path writes its records itself (`banjo_d3d_records_native`, see "Step 5"); recorded command buffers are records run where their indirect buffers are; package 24 |
| 6 | No PM4 at all: kicks, fences, Swap and BlockUntilIdle on host fences; the ring thread retired (the decoder and the translator are what is left) | the ring | **built, on**: kicks and Direct3D's own ring packets through a host queue (`banjo_d3d_host_submit`, see "Step 6"): the primary ring is not read; fences, Swap and interrupts are record operations the executor runs in stream order; the PM4 parser stays as the fallback (about 2-3 % of the words on the PC); package 24 |
| 7 | Parallel translation: the draws of a recorded command buffer (one render pass) translated by two threads into Vulkan secondary command buffers | the translator's per-draw work (`DrawsVulkanImpl::Draw`, ~15 % and the stages around it), over two cores | 7a (`banjo_ring_vs_hash_given`), 7b the prover's finer clearing (`banjo_ring_prover_fine`), the loads cleared the same way, the record release off the executor (see "Step 7b", "Step 7, more"); parallel translation waits for package 24's console profile |

Each step has its key, verify check, docs and in-run A/B. On 2026-10-16 the user asked for all of them (and the CPU
codegen agent's round, and a final ARM audit of the new code as a tenth step) before the next console test: one
package at the end.

What each step is worth, from the console profile below: the decoder (3) and the transport steps (4-6) take decode work
off the ring, but a record transport alone does not (see "What the profile changed"); the frame-rate limit falls only
as far as the translator's own work (7) is spread over cores. Expected order of the gains: deferred recording (0) and
the prover (2) first, then the decoder (3), then parallel translation (7).

## Step 0 results (PC)

- Deferred recording with draw packets, in-run A/B target 16, console settings, intro route: the ring 8.37 us a draw
  with the layer on against 4.01 off; the recording part 3.06 against 3.17; 668,000 records in 7,525 queue slots per
  10 s (was 665,000 slots of one call each). The rest is the wait for the worker at the frame's end: the PC's driver is
  so cheap that its worker is the slower thread. The picture check (`tools/ab_flicker.py` sheet) shows only motion at
  the switches. On the console the driver calls are a third of the ring's work, so the worker should be the faster
  thread there: the console A/B decides.

## Step 1 results: the census (PC, console settings, intro route to the level, 340 s)

Every function that writes after a device's write pointer, counted per Swap in the level by device
(`tools/d3d/d3d_census_build.py` builds a tree with a counting hook on each of the 73 writers; `--restore` puts the
normal generated code back; `tools/d3d/d3d_census_report.py` reduces the log):

| Writer | Device | Calls a Swap |
|---|---|---|
| `DrawIndexedVertices` (826567C8) | the two recording devices (D1 685, D4 473) | 1,158: 88 % of the PM4 |
| `82223270` | main | 107 |
| `SetShaderGPRAllocation` (82220B20) | main | 24 (72 indirect buffers) |
| `8226AAB8` (the re-send block) | main | 21 |
| `8222AA18` | main | 19 |
| `BeginVertices` (8222CC50), standalone FlushState (82222888), `82657CB0` | main | 17 each (FlushState: 25 indirect buffers) |
| `DrawVertices` (8222C7A0) | main | 13.5 |
| `DrawVerticesUP` (8222E520) | main | 8 |
| `Clear` (822812C8) | main | 5 |
| `822693B0` / `82269618` (the clear-like pair) | main | 3.3 each |
| `82658678` | main | 3 |
| `82230838` | main | 2 |
| tiling `8227A9F0`, `8227B218`, `82278C08` | main | 1 each |

Devices: main `40E6BA00`; recording `452B4F80` (D1), `452BC080` (D2), `418E2B80` (D3), `41860080` (D4). So the
replacement covers one draw function on the recording devices and about 25 small writers on the main device.

## What the profile changed (console, package 21b, title screen, stock clocks)

The ring thread's 69 ms a frame by function (the CPU sampler's last 30 s report, `tools/switch-cpu-profile.py` with the
build's ELF; line-level by `addr2line`):

| Part | Share of the ring | ms a frame |
|---|---|---|
| Content checks: `CrcFingerprint` 10.9 %, `SampleFingerprint` 1.1 %, XXH3 1.0 %, `memcmp` 3.5 % (the shader load memo's microcode compare among them), `Equal` 1.0 % | ~17.5 % | ~12 |
| `DrawsVulkanImpl::Draw` itself (spread over its 2,600 lines: no single hot spot) | 14.5 % | ~10 |
| PM4: `WriteConstantRunRaw` 4.0 %, `Type3` 3.0 %, `Packet` 2.5 %, `LoadMemoHit` 1.0 %, shader identity 1.6 % | ~12.5 % | ~8.6 |
| NVK (`vk_dynamic_graphics_state_copy` 3.9 %, descriptor sets 2.2 %, cbufs, shaders, vertex input) | ~12 % | ~8.3 |

Two facts decide the order of the work:

1. **A record transport alone does not buy ring time here.** `banjo_direct_state` (records decoded from the PM4, the
   ring side of any record transport) lost on the console twice (47.2 against 45.0 us a draw; d3d-native.md section
   3): the PM4 parser is vectorised already, and a record pays for its index, release and checks what it saves in byte
   swaps. Records from native code have the same ring side. The replacement pays where it moves work to another
   thread or makes it unnecessary.
2. **The largest single cost is reading guest memory to prove it unchanged** (the persistent vertex and index blocks:
   about 1.9 GB per 10 s, 13.8 MB a frame in some 670 windows). Lock tracking cannot replace it: loading writes vertex
   memory with no Lock (the lock rule's first run, 2026-10-14). But the bytes of a submitted draw can be read earlier,
   on another core: step 2.

## Step 2: the ring prover (`banjo_ring_prover`, 2026-10-16)

`app/src/me/masseffect/banjo_ring_prover.h`. The ring records, per indirect buffer (by guest address), the windows it
had to fingerprint while running it. When the game moves the write pointer, the prover thread (core 0 by
`banjo_ring_prover_core`, the ring's priority) parses the newly submitted packets of the primary ring for indirect
buffers (headers only, nested buffers too) and their waits, clears the packets in stream order while every wait in
front of them is released (it tests each WAIT_REG_MEM as the ring would), and fingerprints the windows each cleared
packet's buffers needed in their last run. A fingerprint is published with the cleared position taken before its bytes
were read; the ring uses it for a draw whose packet ends within that position, and otherwise reads the window itself
as before. The first version voided a fingerprint at every wait the ring passed (and every interrupt): 25 % of the
windows taken on the PC; the cleared rule took 35-65 %.

Why the bytes are the same: the rule of `banjo_ring_vertex_persistent_submitted` (on since 2026-10-14): a submitted
draw's inputs are final until the game learns the GPU has run it. Behind a WAIT_REG_MEM the game may still write until
it releases the wait, hence the clearing (a game that gates data sets the gate before it submits the wait, since the
console's GPU could reach it at once). GPU writes: memexport goes to the renderer's mirror, never to guest memory, and
a memexport window is redirected before the persistent blocks are consulted. The persistent block
records the prover's submission position, not the ring's, as the moment its bytes were read.

Checked: every use with `banjo_cpu_verify`, the first 4096 uses in every configuration, fingerprinted again on the ring
and compared; a difference switches the prover off for the session. In-run A/B target 42
(`configs/banjo_ring_prover_ab.toml`); the "ring prover" report line gives the ring's takes and its own reads by reason,
and the prover's batches, buffers, windows and time.

Expected on the console: most of the fingerprinting's 10.9 % of the ring (about 7 ms a frame) moves to core 0, which
the main thread uses about a fifth of. What the A/B shows is the measure.

## Step 3: the ring split (`banjo_ring_decoder`, 2026-10-16)

`app/src/me/banjo_ring_split.h` (the queue and the dirty-page tracking) and `me_native_system.cpp` (`ParseLoop` on the
decoder thread, `ExecutorLoop` on the ring thread). Design:

- **Threads.** The ring thread is the executor: it is the guest thread the interrupts run their guest code on, and the
  thread the renderer (and deferred recording's producer) has always run on. A new decoder thread (core 2 by
  `banjo_ring_decoder_core`, the ring's priority) runs the old parse loop.
- **The register file.** The decoder keeps the ring's file and marks the pages (64 registers) it writes: WriteRegister,
  the run writers, and the few direct writes (the rectangle split, the dead-raster draw, a predicated draw's initiator,
  MakeCoherent, a second-tile draw's DMA registers). Every item it queues is preceded by those pages, so the executor's
  file, which the renderer reads, is the decoder's at that point. Every 1024th draw (16th in verify mode) also carries
  the whole file for a compare. (Pages of 16 registers: pages of 64 moved 3.5 MB a Swap on the PC.)
- **What the game sees.** Memory writes (REG_TO_MEM, MEM_WRITE, the fences of EVENT_WRITE_SHD with the counter read when
  written, the scratch write-back), the tiling extents, interrupts, the GPU-wait notifications and the read pointer
  write-back are items the executor performs in stream order: the game learns of progress only once the draws before
  it have been executed, as with one thread.
- **Answers the decoder needs.** Copies (the tile passes use their results), the Swap (Present, the A/B bookkeeping),
  the tile check, the 10 s reports, a COND_WRITE and an occlusion query (both read memory the executor may still be
  about to write), and making the renderer: call items the executor runs after everything before them while the
  decoder waits.
- **WAIT_REG_MEM** stays the decoder's: it must not decode past a wait the game has not released. Its 200 ms timeout
  does not run while the executor still has items (the executor's writes may be what it waits for).
- **Stable inputs.** Each draw's vertex program goes in a kept copy (keyed by the program's hash): the stage's vectors
  change with the next load. The MMIO reads of the game see the executor's file.

The prover follows the executor: each draw names its innermost indirect buffer (`PieceSwitch`), and the executor
publishes the stream position of the draw it runs.

## Step 5, first part: the records with the split (2026-10-16)

The records Direct3D's draws become (`banjo_direct_state`: each draw's state packets decoded into a host record on the
game thread, the region covered by one marker NOP) now work with the ring split: the decoder indexes and applies them
where the ring did, and they are released at the Swap, which runs on the executor while the decoder waits. PC verify
(300 s, console settings): 5.2 million records checked at the draw and 6.9 million at the decoder, 0 differences.

Measured (PC, in-run A/B target 1 with the split on, 300 s): decoder 2.59 ms per Swap with the records against 2.65
without; the executor the same within noise (9.32 / 8.87 ms, the scenes of the halves). So the record transport gives
the decoder nothing, as it gave the single ring nothing on the console (47.2 against 45.0 us a draw): the decoder applies
a record with the same register functions the packets go through. `banjo_direct_state` stays off.

## Step 4: records of every writer (`banjo_d3d_records`, 2026-10-16, in progress)

The user's order after step 3 (2026-10-16): do steps 4-6 even though they no longer buy frame time, because the end
state, a renderer that parses no PM4, is the simpler design other ports can reuse; then speed up the executor (step 7)
and audit all of it for the A57.

The design follows sa2-nx's (docs/api-renderer.md there, section 4): host records of operations, named by a marker NOP
in the command buffer while Direct3D still keeps its command buffers, the queue alone at the end. What Banjo adds:

- **Whole calls, every writer.** `banjo_direct_state` made a record of a draw's state packets only (up to the draw
  packet). `banjo_d3d_records` makes records of everything an outermost Direct3D call writes: the draw packets and the
  tile wrapper around them, events, fences, interrupts, the Swap, shader loads, the clears' and resolves' packets. The
  calls are the draw hooks, Swap and Resolve (banjo_me_hooks.cpp), and 60 generated hooks
  (`tools/d3d/d3d_record_hooks.py` writes `app/src/me/banjo_d3d_record_hooks.inc` from the static writer scan,
  `tools/d3d/d3d_writers.py`, less the space functions and the functions hooked already). A counter in the thread's
  guest context (`PPCContext::host_counters[63]`) makes only the outermost hooked call convert; a writer called inside
  another is part of the outer call's region. A writer's device is its r3 when the draw hooks have seen that device,
  else the device Swap is called with (the real device).
- **Pieces.** A call's packets are cut into pieces, one record and one marker each. What stays PM4 where it is:
  indirect buffers and WAIT_REG_MEM (the ring prover reads them in the command buffer: it clears the stream at the
  waits it sees released and follows the buffers into the recorded command buffers; step 6 moves both to host
  queues), and at the start of a piece a packet whose words must stay in place under the marker (a plain NOP, whose
  payload may be inline data a later packet points at; a tile mask Direct3D patches after the call; another
  record's marker): the piece starts after it.
- **What a piece carries.** Register runs in host byte order (the decoder writes them with `WriteConstantRunHost`, as
  the draw-state records did), type-1 pairs, every other type-3 packet with its own header (predicate bit included),
  address and words: the decoder runs it through the same `Type3` with the same `packet_address_` and tile-pass state
  as the PM4 parser, so the tile passes (`banjo_gpu_tiles_once`) and the dead raster (`banjo_gpu_dead_raster`) stay
  on (they were off with the draw-state records, which skipped packets they follow). The tile masks are read by
  reference from the command buffer (Direct3D patches them after the call). The renderer's own Clear and Resolve
  marker NOPs are carried as packets; plain NOPs are dropped (their words stay in the command buffer inside the
  marker's region, for whatever points at them).
- **Lifetime.** As for the draw-state records (the ring of records, the index, the side store for command buffers
  run again after `banjo_direct_state_keep_swaps` Swaps): unchanged.
- **Checked.** At the call, the first records and every record in verify mode are re-encoded and compared with the
  packets the call wrote, word for word (`SamePiece`). At the decoder, the first records and every record in verify
  mode are decoded again from the region as the command buffer holds it when the decoder reaches it (replays
  included) and compared (`RecordMatchesRegion`): this is what catches a patch Direct3D makes after the call outside
  the tile masks. A difference stops new records; the region's packets are used.
- **Report.** "Direct3D records" line: calls converted, records, the words left as PM4 by reason, and the PM4 the
  decoder still parsed outside records (packets and words); "PM4 parsed outside records" lines break that down by
  where it was (the primary ring, the buffers it runs, deeper, Direct3D's fixed buffers) and opcode. In-run A/B
  target 45 (`configs/banjo_d3d_records_ab.toml`).
- **Calls that overlap.** Two threads can be inside Direct3D calls on one device at once (found by the decoder's
  check on 2026-10-16, 50 s into a run, when the game's save thread used the real device while the main thread's call
  waited inside Direct3D): their regions overlap, each holding the other's packets, and the second record's marker
  landed inside the first record's region. Each device slot counts its outermost calls in progress and begun (one
  read-modify-write at each end of a call, on a cache line of its own): only a call that began alone, ended alone and
  saw no other call begin makes records; the others stay PM4 (counted: "overlapped another call").
- **Calls that change segment.** A call that runs out of room gets a new segment from Direct3D's space function
  (sub_82210AB0, which may submit the old one). Its hook moves the call's scope to the new segment
  (`NoteSegmentSwitch`, a thread-local scope): what the call writes there is made into records; the words in the old
  segment stay PM4. (First run: 184,222 calls in 300 s were left whole as PM4 for this.)

First run (PC, console settings, intro route, 300 s, `banjo_cpu_verify`): 9.05 million calls made into 9.38 million
records, 897 million words the decoder did not parse; every record checked at the call (9.38 million) and at the
decoder (12.8 million, tile replays included): **0 differences**. Still parsed as PM4: 89 million words (9 %): the
barriers (4.0 million words), unmarkable heads (1.6 million), partial packets at a call's end (1.9 million: a
packet whose words the game writes after the call, such as BeginVertices' inline data), the calls that changed
segment (since handled, above), the primary ring's indirect buffers (step 6) and Direct3D's fixed buffers.

## Step 5: the native draw path writes its records (`banjo_d3d_records_native`, 2026-10-16)

With `banjo_d3d_records`, the draw hooks hand the native Direct3D draw path (`banjo_d3d_native.h`) its device's
staging area (`RecordOps`, `banjo_records.h`; one per device of the hooks' table, a device being drawn on by one
thread at a time). The native code writes each record operation as it writes the packet: the register runs from the
shadows in host order (`ConstantRunsOps`, `RegisterRunsOps`, `FetchRunsOps`, one rev32 per four words), the draw
packet and the tile wrapper around it (`EmitType3`, `EmitRef`). What Direct3D's own helpers write in between (the
draw-time shader load, the re-send block, the 0x2388 block, a block that did not fit) is decoded from the command
buffer right after the helper returns (`HelperOps`). The hook publishes the staged operations as one record for the
whole call: no decoding of the native packets on the drawing threads.

The packets are still written. The decoder never reads them, but they are what makes the rest simple: a command
buffer run again after its record left the ring is parsed (the side store keeps three words a record, not its
operations), and they are what the producer's check compares the native operations with (`SamePiece`: the first
4096 native records, every record in verify mode). Writing them costs the drawing thread little (stores of words it
has in registers); what the records save there is the decode pass. Anything the operations cannot describe leaves
`RecordOps::cur` null, and the hook decodes the region from the packets instead: a helper that wrote a barrier or
changed segment, the original finishing the call (a decline), a checked call. A difference stops the native
operations for the session (the decoding goes on). In-run A/B target 46.

## Step 6: the kicks through a host queue (`banjo_d3d_host_submit`, 2026-10-16)

Direct3D's kick (`sub_82210408`: r3 the device, r4 a list of (length, address) pairs, r5 the count) writes one
INDIRECT_BUFFER per segment into the primary ring (header `0xC0013F00`, from the device's ring write position +10952
with the mask +14900, in words) and then the write pointer (device +10952 and MMIO `0x7FC80714`). Its hook
(`KickBegin`) works out the packets from the arguments before the original runs; `WriteMmio`, when the write pointer
stored is the one predicted, puts them into a host queue before it publishes the pointer. The decoder takes a queued
segment where its packet is in the ring (by position) and runs it as that packet (`SubmittedSegment`: the same
`Type3` of the indirect buffer, from the queue's words), without reading the ring; whatever else is in the ring
(packets no kick wrote: the ring's set-up) is parsed as before, and an entry the decoder went past is dropped at the
end of the window. Checked: the first 4096 queued segments, all in verify mode, against the ring's three words. The
kick still writes the ring and the pointer: Direct3D's own space accounting reads them, and the ring prover still
reads the primary ring for the indirect buffers it follows (not yet the queue). In-run A/B target 47.

First run (PC, 300 s, verify mode): 126,375 kicks, every segment queued and run from the queue, every one checked
against the ring's words: 0 differences; the decoder parsed 3 packets of the primary ring itself (its set-up).

Direct3D also copies packets of its own straight into the primary ring between kicks (`sub_826571D8`: the next
segment's tile select, 2-3 a frame; the decoder parsed 22,383 of them in the first check-all run). Its hook decodes
them into record operations (by value: the ring's words are final once written) and queues them at the ring position
they will have, for the next kick's write pointer to submit; the decoder applies them there. The queue's producers
take turns (a kick's prediction is kept per thread: two threads can be in Direct3D on the real device).

## Step 6b: the barriers in records, and Direct3D's fixed buffers (2026-10-16)

- **Barriers in records** (`banjo_d3d_records_barriers`). The indirect buffers and WAIT_REG_MEM were left as PM4 for
  the ring prover. Now records carry them too, and the prover reads a marker's record for what it follows
  (`ParseRecord`: the indirect buffers, waits and shader loads in it, with the barrier counts of step 7b). The records
  are in their ring until the executor releases them at a Swap, after the decoder used them; the prover indexes them
  itself as they are published (`RecordIndex`) and reads one only while it is not released (the tail checked before and
  after the read; a record read across its release is taken back). A record it cannot read (released: a command buffer
  run again a frame or more later) holds the stream there, as an unreleased wait would. With the barriers inside, a
  call is one piece unless something else stops it. This also gives step 2b its loads back (the prover saw no IM_LOAD
  inside records before).
- **Direct3D's fixed buffers** (`banjo_d3d_records_static`). `1FB00000` and `1FB01980` (per-tile set-up and clears,
  run before every recorded buffer; 35 million words parsed in a 300 s run) are written once by Direct3D and never by
  a hooked call. A buffer with no record in it that runs a second time with the same words is decoded into operations
  once, and later runs compare its words with the copy kept from that run (exact, not a fingerprint) and apply the
  operations; a buffer the game rewrites is decoded again. At most 256 such buffers of up to 4096 words. The prover
  still reads these buffers' packets itself (they are not records to it).
- **The marker's head rule, relaxed.** A packet whose words must stay in place (a plain NOP's payload, a tile mask
  Direct3D patches) could not start in the marker's three words. It may start at word 2: its header is then the
  marker's last word (the record carries the header) and its payload from word 3 on is untouched. Most of what the
  recorded buffers still left as PM4 was a stateless tiled draw, `[VGT_INDX_OFFSET run (2 words)][tile mask]...`, cut
  into a two-word piece too short for a marker, the mask, and the rest.
- **The segments a call left.** A call that runs out of room continues in a new segment; its words in the old one were
  left as PM4 (167,000 calls in 300 s). A recording device's space function only chains its segments, nothing is
  submitted: the space hook keeps the part of the old segment the call wrote (the write pointer before the space
  function ran), and when no submission happened during the call, that part is made into records too, before the
  new segment's part (stream order).

First run with all of it (PC, console settings, intro route, 300 s, verify mode, 0 differences; before the segment
parts): 7.6 million calls made into 7.6 million records (one each: no barriers to cut at), 740 million words the
decoder did not parse, and Direct3D's fixed buffers applied from records 453,944 times (25.9 million words); the
primary ring: 5 packets parsed in 300 s (its set-up), every kick and 36,589 words of Direct3D's own ring packets from
the queue; the prover read 11.0 million records (none unreadable) and took 92 % of the executor's windows, 5.25
million uses compared, 0 differences. Left as PM4 in the command buffers: about 4 % of the words (much of it plain
NOPs' payloads: inline vertex data, read in place), mostly the old segment's part of the calls that changed segment
(since recorded, above, for the recording devices).

With the shipped defaults (steps 4-6b on, 2026-10-16 evening): verify mode, 300 s, 0 differences; and a normal-mode
run with every record checked (`banjo_diag_records_check_all`: verify mode runs every native draw through its
device-copy check, so the native draw path's own records are only made outside it), 300 s, 0 differences: 10.1
million records (991 million words the decoder did not parse), 7.94 million of them written by the native draw path
and every one compared with its packets. A recording device never submits its segments (they run as indirect buffers
of the real device once recorded), so a kick during one of its calls no longer keeps the call's packets as PM4.

## Step 7b: the prover clears a held packet up to its held wait (`banjo_ring_prover_fine`, 2026-10-16)

The first records run showed where the executor's own fingerprinting comes from: of the windows it needed in 10 s,
the prover had 128,000 ready and 245,000 were "beyond the cleared stream" (PC, console settings). A primary-ring
packet is most often one indirect buffer to a Direct3D segment, which runs the recorded command buffers (indirect
buffers of its own) and holds Direct3D's waits for the game (`WAIT_REG_MEM` on 1FB04002, 1FB04006, 1FB04016: 6-7 ms a
Swap of the decoder's time). The prover cleared a packet whole: the draws of every buffer in front of the segment's
last wait waited for it, although the decoder, which waits on each wait in stream order, had passed them already.

So stream positions get a second part below the packet: the number of barriers (indirect buffers and waits) the
segment has started. The decoder counts them for each draw (`RingProgress::sub`, in `Type3` at depth 1, predicated
packets included) and the prover as it parses the segment (`ParseBuffer`), the same packets in the same order. A held
packet is cleared up to its first unreleased wait: the buffers that start before it are fingerprinted at once
(`through` = the packet's end, scaled, and the wait's count less one). Positions are scaled in the table and at `Take`
(`Scaled`: position << 10 | count; a whole packet is 1023); what the persistent blocks keep as their submission is the
packet before (`Unscaled`) unless the whole packet was cleared, so the persistent rule never takes more than before.
A segment's own draws (not in a recorded buffer) need the whole packet, as before. In-run A/B target 48.

First run (PC, console settings, intro route, 300 s, verify mode: every use fingerprinted again and compared, 0
differences): per 10 s the executor took 350,130 fingerprints from the prover (4.16 GB it did not read) and read
26,515 windows itself (93 % taken; before: 128,081 taken, 244,683 read). The prover's own work grew accordingly: 6.8
GB fingerprinted in 0.75 s per 10 s on the PC (it was 1.8 GB), on core 0.

Found on the way and fixed: with the ring split, the decoder's shader-load fingerprints (step 2b) were taken against
the executor's stream position, which lags the decoder's: a load could take a fingerprint read before the load's
packet was cleared. The decoder's own position is used now.

In-run A/B on the PC (target 48, normal mode, 300 s, profiler on): the executor 9.32 ms of work a Swap with it, 9.73
without (4 % less); the PC fingerprints at XXH3 speed, so on the console, where fingerprints were 10.9 % of the
ring, the share should be larger.

## Step 7, more (2026-10-16 evening)

- **The shader loads cleared the same way.** Each load the prover notes keeps its barrier count, so a held packet's
  loads before its held wait are fingerprinted too, and the decoder takes them at its own position and count. Verify
  run (300 s): 524,071 loads per 10 s compared by fingerprint, 171,238 by their words (before: 115,529 and 728,773);
  every verdict also compared with the byte compare in verify mode: 10.1 million, 0 differences.
- **The record release off the executor.** At each Swap the executor ran the decoder's record release (indexing the
  ring's cold headers, checking markers in command buffers): 1.2 % of its samples on the PC. It is the decoder's
  state, so the decoder does it now, right after the executor's Swap returns.
- **What the executor's profile shows next** (PC, `banjo_pc_profile_s`, the executor thread): the driver about 12 %,
  `DrawsVulkanImpl::Draw` itself 10 % (spread over cache lookups: the index cache entry, the texture and sampler
  caches, the persistent vertex entry, the pipeline key), the window fingerprints it still makes 9 % (the A/B's off
  halves included), copies 7 % (vertex windows and constants into the upload buffers), then texture entries,
  `CopyVerticesFast`, pipeline lookup. The console's profile of package 24 decides what comes next: the driver's part
  can go to deferred recording's worker (`banjo_deferred_ab.toml`, and `banjo_deferred_ab_core0.toml` with the worker
  beside the main thread, since the decoder now has core 2).

## Package 24b on the console (2026-10-16 evening; CPU 1020, GPU 460.8, RAM 1600 MHz, handheld)

Six runs of 3-4 minutes in play (diagnostics, fragments, and the A/B configs of targets 48, 45 and 16 twice). No
crash, 0 differences in every run.

- **Frame rate**: median 17-19 fps in play (16.9 with the diagnostics' overhead), packages 21b and 22 15.2 and 14.9
  on the same clocks. Frames snap to the display's 16.7 ms: mostly 66.7 ms frames (15 fps) with 50 ms ones (20 fps),
  so a frame that misses 50 ms costs a whole refresh.
- **The executor** (the ring thread): 20.8 us a draw (21b: 26.2, -21 %), 45-54 ms of work a Swap, 68 % of its core:
  no longer pinned at 100 %. Its fingerprinting fell from 10.9 % of its samples (21b) to 2.8 % (`CrcFingerprint`) plus
  1.6 % (`SampleFingerprint`): the prover took 81 % of its windows (153,459 per 10 s against 35,723 it read itself).
  Draw stages per timed draw: recording 10.8 us (the driver calls), uploads 7.4 (21b: 11), pipeline 4.8, indices
  4.4, textures 3.8.
- **The decoder** 21 % of its core (10-11 ms of work a Swap), most of its time waiting for the executor's answers
  (SplitCall) and the game's waits. Records cover all but about 1.4 % of what it reads; every kick came through the
  host queue; 4.57 million native draw records in the diagnostics run, the 4096 checked all equal.
- **The prover** 38 % of core 0: 3.2 GB fingerprinted per 10 s, and its own loop 17 % of its samples (19,160 wakes
  per 10 s, 3.17 million windows asked, 2.94 million already covered). The next CPU item.
- **All three cores busy**: the threads' busy shares add up to 285 % of the 300 % the process has (diagnostics run;
  230-260 % in the A/B runs): the frame is limited by the CPU as a whole, not one thread (executor 68 %, prover 38 %,
  main 29 %, the audio thread with the reverb 28 %, a recording thread 28 %, the decoder 21 %, another game thread 20 %,
  the diagnostics' shader-statistics thread 15 %).
- **GPU**: 56-58 ms a Swap in the A/B runs, of it 15-20 ms gaps (waiting for work): about 38-41 ms busy (scene 20-23,
  EDRAM 9-pass imports 6-7, copies about 6). At this GPU clock the GPU alone would allow about 25 fps.

In-run A/B results (per draw on the executor, Swaps per half as the frame rate; the halves alternate every 4 s):

| Target | Executor us a draw, on / off | Swaps on / off | GPU ms a Swap on / off |
|---|---|---|---|
| 48 `banjo_ring_prover_fine` | 22.79 / 23.86 (-4.5 %) | 1978 / 2027 (-2.4 %, scene noise) | 57.6 / 56.4 |
| 45 `banjo_d3d_records` (decoder 10.73 / 10.18 ms a Swap) | 21.92 / 22.79 (-3.8 %) | 2111 / 2112 | 56.5 / 56.1 |
| 16 deferred recording, worker on core 2 | 19.39 / 21.62 (-10.3 %) | 2287 / 2217 (+3.2 %) | 56.4 / 58.2 |
| 16 deferred recording, worker on core 0 | 19.70 / 22.43 (-12.2 %) | 1923 / 1832 (+5.0 %) | 56.7 / 59.3 |

So the records (steps 4-6) cost the decoder about 5 % and the drawing threads about 3-4 % of their time (the
producer), and buy no frame rate, as expected; the prover's finer clearing helps the executor a little; deferred
recording, slightly worse before the split, now gives 10-12 % of the executor's work a draw and 3-5 % in frame rate.

## The prover removed (2026-10-16 evening)

The run with `banjo_ring_prover_off.toml` (package 24b, the same route) settled step 2. With the prover off the CPU as a
whole was at a median 202 % of 300 % (227-280 % in the six runs with it), the frame rate the same (17.8 fps against
16.9-18.8), and the executor's work a draw no higher: 48.6 ms a Swap for 1,979 draws, 24.6 us, against 25.0-26.1 us
in the runs with the prover and the same configuration otherwise (the uploads stage rose from 7.4 to 9.9 us a draw with
the fingerprints back on the executor, and the recording, textures and pipeline stages fell by about as much: the
prover's 320 MB/s of reading and its core were costing the others). The prover itself had been busy 41-43 % of a core,
93 % of the windows it was asked about already covered and half its time in CRC.

So the step is gone: `banjo_ring_prover`, `banjo_ring_prover_loads`, `banjo_ring_prover_fine`,
`banjo_ring_prover_core`, A/B targets 42 and 48, `banjo_ring_prover.h`, the decoder's barrier count per draw
(`RingProgress::sub`), the innermost-buffer names (`PieceSwitch`), the published positions and the records' second
reader. The executor fingerprints the persistent windows itself (as in packages 21b and 22), and shader loads compare
their words (the load memo and the same-program test as before step 2b). PC check: `banjo_cpu_verify` 240 s with the
console settings and the intro route, 0 differences.

Also in package 25: deferred recording (step 0) is on by default with its worker on core 0
(`masseffect_native_deferred_recording`, `banjo_deferred_core = 0` in `renut.toml`), from package 24b's A/B target 16:
-12.2 % of the executor's work a draw and +5.0 % Swaps with the worker on core 0. `configs/banjo_deferred_off.toml`
turns it off for a run, `banjo_deferred_ab_core2.toml` compares core 2.

For other ports: a second thread that re-derives what the main path already knows (here: which bytes a draw will read,
and when they are final) costs more than the work it moves, unless the CPU has a free core. If the fingerprints are to
leave the executor, the decoder is the place: it already walks the stream in order, past every wait, and has 30-40 ms
of slack a frame.

## Step 10: the ARM audit of the new code (in progress)

The user's tenth step: every piece of new code of steps 2-7 judged for the Switch's A57 (in-order-ish issue, 2 MB L2,
no LSE atomics, `thread_local` a function call, write-allocate misses on a large ring). Items so far:

- **The ring of records' write misses** (`banjo_records_zero_ahead`, from sa2-nx's round 11): the ring is 8 MB, four
  times the L2, so every record line was a write miss that read DRAM first (sa2-nx's console profile: most of its
  publish's samples). After each publication the producer zeroes the next 2048 words of free space with DC ZVA
  (allowed in user mode when DCZID_EL0.DZP is clear; the block size from DCZID_EL0.BS), whole blocks only, each once,
  never past the consumer's tail plus the ring's size. **DC ZVA zeroes the whole aligned block the address is in**:
  package 24's ring came from `make_unique` (16-byte aligned), so each zeroing could wipe up to the last 12 words of
  the record just published; the console's first run caught it at once (the decoder's check, "first difference at
  operation word 2472 of 2480", and the records switched themselves off). The ring is 4 KB-aligned since 24b, the
  zeroing turns itself off on an unaligned ring, and the PC's memset zeroes the same aligned blocks DC ZVA would, so
  the PC's tests now see what the console does. (sa2-nx's ring is allocated the same way: to tell its session.)
- **No thread detach on the Switch.** libnx has no detach (newlib's `pthread_detach` calls a syscall libnx does not
  implement), so `std::thread::detach` throws `std::system_error`: the prover's thread (step 2) ended packages 23,
  23b and 24 with `std::terminate` at the title screen. Since 24b it is the SDK's own thread
  (`rex::thread::Thread::Create`, 512 KB stack), kept for the life of the process, as battd_nx and fruitninja_nx keep
  their libnx threads (`threadCreate` with an explicit stack, never detached or closed).
- **Byte swaps four words at a time.** The records' register runs come from big-endian shadows and packets
  (`SwapWords`): an explicit NEON `rev32` per 16 bytes on the A57 (sa2-nx's round 9 finding: a word loop the compiler
  does not vectorise costs several instructions a word). Checked in the Switch build: the native draw path's
  operation writers have 141 `rev32`, the producer (`EndWholeLocked`) its own; no thread-pointer read and no atomic in
  the native draw path.
- **Thread-local storage is a call on the Switch** (`-mtp=soft`: every access is a `bl __aarch64_read_tp`). The first
  build read the per-thread segment scope two to four times a call, behind a dynamic-initialisation guard (its
  members had default values). It is plain data now (zero-initialised storage, no guard), read once at the call's
  start and kept in the call's `DrawDirect` for its end. The decoder's register writes marked their dirty pages
  under `t_decoder` (step 3): `WriteRegister` alone had 8 thread-pointer reads; the marking now tests the member
  `split_on_` (the executor writes registers only inside a SplitCall's closure, while the decoder waits: a page it
  marks is sent again, which changes nothing).
- **Exclusive loops for report counters.** The count of nested hooked calls (the most frequent hooked calls: 41
  million in 300 s) was an atomic add, a load/store-exclusive loop on the A57 (no LSE atomics in ARMv8.0); it is a
  plain relaxed load and store now, like the other report counters of the producers. What stays atomic per call:
  the device's call count (`DeviceCalls`: one read-modify-write at each end, on a line of its own), the producers'
  lock (`g_producing`) and the marker handshake (`g_marking`: a store-release and a load-acquire).
- **Checked and left as they are:** the kick queue (per kick: a few a frame), the fixed-buffer cache (a map lookup and
  a memcmp of 74-600 words per run, instead of parsing them), the prover's record reading (sequential reads of the
  record ring; its vectors grow per primary-ring packet as before).

## What steps 4-6 would still take (the PM4 retired)

With the split, PM4 decoding is off the thread that limits the frame rate, so these steps no longer buy frame time; what
they would buy is the decoder's CPU (about 3 ms a Swap on the PC) and a design without PM4 for other ports. What is
left, concretely:

1. **Native writers for the main device** (step 4): `DrawVertices` is native already (`banjo_d3d_native_draw_vertices`),
   Clear and Resolve have markers (`banjo_d3d_api_clear`, `banjo_d3d_api_resolve`); `DrawVerticesUP`, `BeginVertices`,
   the standalone FlushState, `SetShaderGPRAllocation`, the re-send block, the tiling functions and the rest of the
   census's writers (about 25) would each need a native form writing records, and each a check against the PM4 it
   replaces.
2. **Records without PM4 behind them** (step 5): today a record's region keeps the original packets as the marker's
   payload, so they can be rebuilt when the marker is overwritten and compared when it is reached; a native draw that
   writes only the record needs another fallback (the record kept until Direct3D reuses the segment, which needs
   Direct3D's segment fences) and its check moves to the record itself.
3. **The tile passes** (step 5): Direct3D runs a recorded buffer once per EDRAM tile with predication, and patches a
   draw's tile mask after the GPU wrote the extents; records would carry the predicate and read the patched words at
   run time.
4. **No PM4 at all** (step 6): the kicks, the fences Direct3D waits on (+10896 / +10908), the Swap and BlockUntilIdle
   on host fences, and Direct3D's fixed buffers (`1FB00000`, `1FB01980`: per-tile set-up) as host operations.

Each is exact work of its own (a check against the PM4 it replaces, a key, an A/B); together several days. They are
the route to a renderer with no PM4, not to a faster one; the faster one is step 7 (the executor's own work).
