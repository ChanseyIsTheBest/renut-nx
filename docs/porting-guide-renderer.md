# Converting a ReXGlue Switch port to Banjo's renderer (2026-10-27)

For the other ports (SA2, Sonic Transformed, Marathon-style titles, futari, bejeweled, Mass Effect, ...) that want
what banjo-nx's renderer does today. It follows on from two earlier guides, which still hold for the first steps:
`docs/renut-renderer-port.md` (bringing reNut's renderer into a port) and `docs/deko3d-native-renderer-kit.md`
(the deko3d back end, shaders, the GPU). This one is about the render thread and the CPU around it: what Banjo built
after those, in the order a port should take it, what each piece needs from your game, and how to check it.

Credits and licences: reNut's renderer is masterspike52's (reuse for the user's ports is permitted, credit it);
NetherSX2_nx's 4-core layout is NaGaa95's (MIT); sa2-nx's CPU items are the user's SA2 port's. Banjo's own code is
GPL-3.0 like the rest of banjo-nx.

## 0. Where it lands

| | Banjo, 2026-10-16 (GPU emulation renderer) | Banjo, 2026-10-27 (this renderer) |
|---|---|---|
| City render thread a draw | ~12.8 us | 10.1-11.9 us |
| Draws on the fast path | - | ~98 % |
| Game threads' cores | 3, all full | 4 (core 3 for audio), 263 % of 400 % used |
| Exactness | - | pixel-identical, every item with a verify mode |

Banjo's limit is now the render thread on its own core (about 5,000 draws a frame, half of them shadow-map draws) and
the GPU (40 ms); see section 7 for what is still open. A port with fewer draws per frame gets further with the same
pieces.

## 1. The shape of it

```
game threads ──> hooked D3D setters (write the device, mark the 64-byte chunks they touch)
                 draw / clear / resolve calls inside a BeginCommandBuffer ... EndCommandBuffer
                     │
                     ▼  recording (NewOp, on the game's draw threads)
                 op = { changed chunks (compared from the marks), field bits, constants' dirty groups,
                        record-time translation: texture slots' extents, texture objects, index / vertex
                        buffer header words }
                     │
                     ▼  RunCommandBuffer → queued to the render thread
render thread:  Replay: per op, the changed chunks into the device image, the records into per-depth tables
                FastDrawLocked: only the groups the op's field bits say changed are derived again
                    shaders / declaration → pipeline (cache by render-state contents)
                    geometry → mesh object (whole-buffer copies, proven once per kick by the store cards)
                    textures → texture object's answer (else slot memo → bind)
                    constants → pushed uniforms (only the dirty, not-held registers)
                → plume deko3d command list
```

Two ideas carry most of it. **Do the work where the information appears** (a setter knows what changed; the
recording thread knows a texture's header; the render thread should only apply). **Keep answers until something
pushes them out** (store cards for guest memory, generations for caches) instead of checking them at every draw.

## 1b. How close to Unleashed a game can get

Unleashed's renderer translates each D3D call once, when the game makes it; resources are host objects from their
creation, and a lock says when a buffer's data changes. Three things decide how much of that a ReXGlue port can copy:

| Unleashed has | If your game does the same | If it does what N&B does |
|---|---|---|
| D3D calls translated when made | translate in the hooks (section 4.1 is the start) | N&B records 98.5 % of its draws into D3D command buffers, re-recorded every frame: record ops on the game's threads, replay them as a prefetched stream (4.6c); the translation happens once either way |
| Resources as objects from creation | hook creation, keep a host object per resource | N&B builds most headers in its own memory (5 % of streams, 26 % of textures through creation calls): resolve the words the hardware takes at the draw to objects on the recording thread (4.6, 4.6b). The render thread then works through object pointers as Unleashed's does |
| Lock / unlock says when data changes | invalidate on unlock | N&B writes buffers without locks: the store cards (codegen marks a byte per guest page on every store) prove copies once per kick interval. Hooking the game's loaders (Banjo's "Phase B") cannot remove these proofs; with record-time objects in place it would only move object creation earlier |

Banjo did not build Phase B (loader hooks) for that reason: after 4.6 and 4.6b it would save nothing on the render
thread, and it needs days of reverse engineering per game. A game that creates its resources through D3D should hook
creation instead of 4.6 / 4.6b's record-time resolution.

## 2. What your game decides (census first)

Measure these before porting a piece; Banjo's numbers are in brackets. The census keys print in the 10 s log.

1. **Where draws come from**: recorded command buffers or direct calls (`banjo_renut_diag_fast_census`). [N&B:
   98.5 % recorded, re-recorded every frame.] Recorded buffers make the record-time items (section 4.5-4.7) natural:
   the recording thread is where you translate.
2. **Where resources come from**: D3D creation calls or built by the game in memory (the Unleashed census,
   `UnleashedCensusOn`). [N&B: 5 % of streams, 0.1 % of index buffers, 26 % of textures through creation calls.] A game
   that creates everything through D3D can hook creation (Unleashed's way); one that builds its own headers needs
   section 4.6's objects made at bind time.
3. **How state is set**: hooked setters or inline writes (the tags census in `command_buffers.cpp`). [N&B: 99.9 %
   through hookable setters.] Setter marks (section 4.1) need this.
4. **Memexport**: pending at how many draws (`memexport GPU work` line). [N&B: nearly all; section 4.7.]
5. **Shadow passes**: depth-only targets and their draw counts (`banjo_renut_diag_shadow_census`). [N&B: two square
   maps, 1024 and 768, ~2,200 draws a frame.]
6. **Threads**: which cores are full (`rex_profile.log` per-thread lines). [N&B: render thread 95 % of core 1, game
   threads the rest of three cores, audio 0.4 of a core.]

## 3. Prerequisites

- reNut's renderer running on the plume deko3d back end, with the store cards (codegen marks a byte per guest page on
  each store) and the exact-stream proofs (`banjo_renut_store_cards`, `banjo_renut_exact_streams`). See the deko3d kit.
- The in-run A/B system (`banjo_renut_ab_targets`, 30 s halves, a `[renut A/B]` line per target) and the verify keys.
  Every item below was measured with it on the console; the emulator only proves exactness.
- The tools in `banjo-nx/tools`: `switch_syntax_check.py` (Switch compile of single objects, and a lint for
  self-initialised locals), `switch_frame_sizes.py` (stack frames that grew), `census_by_shadow_sort.py`, `ab_sum.py`.

## 4. The pieces, in porting order

Each has its own key (on by default unless noted), an A/B target, and a verify mode. File references are banjo-nx's.

### 4.1 Setter marks and field bits (record time)
The hooked setters mark the device chunks they write (`native::TakeTouches`); NewOp compares only marked chunks with
the device's shadow and records the changed ones plus **field bits** (`DrawField`: shaders, textures' words, fetch
constants, streams, index buffer, view packets, targets, bools, declaration...). A periodic full compare checks the
marks (`banjo_renut_tags_check`). Port: hook your game's setters (the native D3D layer), map device offsets to fields
(`MixedChunkFields`). Gain: the render thread re-derives only what changed.

### 4.2 The fast path (`FastDrawLocked`)
A persistent host state (targets, pipeline and key, texture words, constants' tracking) updated per field. Anything
unusual (memexport reads, tessellation, point sprites, your game's special shaders) falls back to the full path
(`DrawStateLocked`), which resets the state. `banjo_renut_fast_draw_verify` runs both and compares what they record.
Port: list your special cases first (Banjo's are the fur shells' shader hashes, skin-once, composites).

### 4.3 Persistent shared constants and pushed uniforms
The shared constants block stays from draw to draw; only changed lines are pushed (`PushSharedConstants`), and float
constants push only registers the shader reads that the buffer does not hold (`PushFloatConstants`, per 4-register
group, with shader literal residency: `banjo_renut_literals_resident`, neutral on the console). Port as is; the
register layout is the device's.

### 4.4 Pipelines by content
Render state digested to a content number (A/B 28: -2.1 ms on the console), shaders compared by cache entry not by
object (A/B 41: N&B makes a shader object per mesh), a 1,024-entry fast pipeline cache. Port as is.

### 4.5 Whole-buffer copies, store cards, mesh objects
Geometry is copied whole once a buffer has been unwritten for three kicks (the store cards prove it once per kick
interval afterwards), bound once, drawn with the game's own first index and base vertex. **Mesh objects**
(`banjo_renut_mesh_objects`, A/B 42) keep a draw's resolved copies under its key (layout, shader, each stream's fetch
words and source, base / count / first index, index buffer words): one probe a draw; every copy dropped or entry erased
raises a generation that retires them all. Port: as is; size the table for your mesh count (Banjo: 8,192 entries;
each submesh is its own key).

### 4.6 Resource objects resolved when bound (Phase A, 2026-10-27)
`banjo_renut_resource_objects` (A/B 45, verify `banjo_renut_resource_objects_verify`). On the recording thread
(`RecordResources` in `command_buffers.cpp`), each texture slot whose address or fetch words an op changed is resolved
to a **texture object** (header address + header words; a thread-safe table, made on first use), and each draw's index
and vertex buffer header words are taken. Replay keeps them per depth (`DrawResources`); the render thread's draw reads
the object's answer (slot words, bind effects) and checks only its fetch words, a **binding generation** (raised by
every texture epoch bucket bump, so the buckets are read only after a change), the frame's content check and the
layout epoch. No guest header is read on the render thread. Depth-word and hand-over bindings keep the memo path.
Port: needs 4.1 (op chunks) and the texture memo (`FillTextures`) as the fallback; the verify compares every object
answer with the memo's and counts headers the game rewrote between recording and replay (the record follows the
hardware: the GPU takes the fetch constants and buffer words when the draw is recorded).

Results (Banjo b35, 2026-10-27): emulator verify 19.7 million object answers, 0 differences (including 4,833 headers the
game rewrote between recording and replay, bound by both paths to the same words); console A/B 45 in a static city
spot: render thread 52.07 -> 49.81 ms (-2.26 ms, -4.3 %), faster in 4 of 4 pairs, about +0.6 fps.

### 4.6b Geometry objects (2026-10-27, `banjo_renut_geometry_objects`, A/B 48)
The same model for vertex and index buffers. The recording thread resolves each draw's stream sources (the stream's
fetch words, its source object, stride byte and stream, the source header's base and size) and its index buffer (the
header's Common, Address, Size) to objects (`FindStreamObject`, `FindIndexObject`: one per buffer, a thread-safe table).
The render thread keeps in each object the whole copy the old lookup found, and for a stream the layout, vertex shader
and strides it was found for. A draw whose objects all answer takes their copies: a pointer and a few compares per
buffer. Validity: an answer is void only when a cache entry may have been erased (a generation raised there), not when a
copy is dropped (its entry stays and its proof fails). Proofs are unchanged (once per kick interval, store cards or exact
hash). Do not key such objects by draw: Banjo's first try (mesh objects, keyed by each submesh's base, count and first
index) made 340,000 objects per 10 s for 300,000 answers and measured -0.07 ms on the console.

### 4.6c The replay as a stream (2026-10-27, `banjo_renut_replay_stream`, A/B 46)
Unleashed's render thread takes a queue of translated commands in order. Banjo's equivalent is the recorded op list: make
it a stream the CPU can prefetch. Ops are 64-byte aligned, and a draw's op is exactly two lines (the header with the
dirty masks, then the call without pointers: Draw*UP data by arena offset, and the shaders' words as recorded). While op
i is replayed, op i+2's two lines and op i+1's arena state, resource and extent records and (when it changes shaders)
its shader lookups are prefetched, all from recorded values. Nothing is decoded ahead: Banjo's earlier look-ahead peeked
at the next op's device words and cost +2.15 ms on the A57. (b35 profile: three loads of op lines were 4 % of the
render thread, because a 288-byte op straddled lines.)

### 4.6d The fetch cache (2026-10-27, `banjo_renut_fetch_cache`, A/B 47)
Data vertex shaders fetch by address (bone palettes, instance data) was copied from guest memory into the upload ring at
every kick: about 3 MB a frame in Banjo's city, the render thread's biggest memcpy. Give it the whole-buffer copies'
rules: a range unwritten for three kicks gets a copy in the stream cache's pool, proven once per kick, dropped when it
changes; a draw whose fetch buffers are all kept in one pool chunk binds that chunk as its fetch data. Dynamic data
(written every frame) never gets a copy and costs one check per interval.

### 4.6e Small things with large counts (2026-10-27)
- Per-shader data the constants push reads (the registers it reads, whether it has literals) copied into the shader
  record's draw summary, which the shader lookup already prefetches: the push read them from the shader cache entry, a
  miss at most shader changes.
- The pipeline object's draw-time fields together at its start (deko3d's pipeline object had them before and after a
  kilobyte of blend and vertex state: four to five misses per pipeline bind), prefetched once the fast path knows the
  pipeline.
- The same for the fast path's own pipeline cache entry (2026-10-28): the pipeline pointer and the words its check
  compares on the first line, the key's draw-time fields on the second; the key itself after them.

### 4.6f The recording threads' lookups (2026-10-28, `banjo_renut_record_lookups`, A/B 50)
Record-time objects move lookups from the render thread to the recording threads; they do not make them free. In
Banjo's c4 console run the two recording threads spent 46 % of their op recording (about 6 ms a frame) in the object
tables' misses: a slot's pointer, then the object's key line, one lookup after the other. Three changes, all exact:
- Keys in the table: a slot holds the object's key words beside its number (32 bytes a slot), so a lookup reads one line.
  Claim an empty slot with a compare-and-swap to a busy mark, write the key, publish the number with release; readers
  pass a busy slot over (at worst a second object for the same key, which is harmless when each object is answered on
  its own).
- Batches: read every guest header address of the op first and prefetch them all, then build the keys and prefetch all
  their table lines, then compare. The misses overlap instead of following each other.
- A memo per recording device (each device is recorded by one thread): the last object of each texture slot, stream and
  the index buffer by its key words. A chunk change marks two or three texture slots whose words did not change.

### 4.6g Per-draw cost: what kept paying after the big pieces (Banjo, 2026-10-28)
Once the record-time objects were in, the render thread's profile was a long tail of loads that missed. What worked:
- **Prefetch from what the op already holds.** If your renderer's records live at the guest object's own address
  (Banjo's `guest_records.h`: the VA the game holds is the record), the replay can fetch a draw's shader and
  declaration records an op ahead from the recorded words alone, without the lookup. Do it in stages, each one op ahead
  of the next: the op's lines, then its record lines, then what the record points at (cache entries, declarations,
  cached copies). A stage that has to read a line that has not arrived yet stalls on it. Banjo's first try (looking in a
  shader memo an op ahead, A/B 49) cost more than it saved.
- **Keep prefetches few.** Two helpers that prefetched every object of the next op cost 2 ms a frame of their own: the
  core's miss buffers filled. Skip what the previous op already used, and what the draw will not read (an index object
  has one answer line; a copy already proven this interval needs no store cards).
- **Do not invalidate what did not change.** Banjo's stream cache cleared every geometry answer at its 60-frame sweep,
  whether or not it erased anything; the frame after each sweep took every draw the slow way.
- **Let the compiler see your persistent blocks.** An accessor returning the shared-constants block from an
  out-of-line call hid its identity, so every field mark computed offsets and line masks at run time. Inline the test,
  keep the set-up cold.
- **Read recorded data where it is.** A replay that copies each op's constant groups into a device image only so the
  draw can read them back pays for the copy and the image's lines; a pointer per group into the recording does the
  same with a store. Keep one place that materialises the image (a nested run needs it).
- **Skip what an answer already covers.** When one texture slot of a draw changes, the others need nothing if their
  object, fetch words and every epoch their answer read are the same as when it was given. Guard such a skip with the
  identity of the caller's output array, not just its address: a stack array can come back at the same address.
- **Grow arenas carefully on a 3 GB heap.** Recorded buffers that keep their largest size and grow by doubling ran
  Banjo's heap out (d4 crash): grow by a quarter past 8 MB, give oversized buffers back on reuse, cap the spares.
- **Invalidate by owner.** A game that makes and drops shader objects with its meshes (N&B does, every frame) destroys
  records all the time. Clearing the whole fast-path state at each destruction sent the next draw down the slow path
  for nothing; clear it only when it holds the record that goes (compare the pointers), and drop only the memos that
  name records.
- **Give a lookup's misses work to hide behind.** Batching lookups (4.6f) overlaps the misses with each other, but the
  probe still follows its prefetch at once. Split the lookup in two around independent work: plan (headers, keys,
  table-line prefetches) before the op's state copy, probe and record after it. Same for a compare against a shadow
  copy: prefetch the marked chunks' shadow lines together before comparing them one by one.
- **Send the GPU only what changed (deko3d).** deko3d's binds are per piece: `dkCmdBufBindShaders` binds each stage
  on its own (a stage in the mask without a shader is disabled), `dkCmdBufBindTextures` takes a first id. Rebinding
  both stages when one changed, or all of a stage's texture handles when one changed, costs command words on the
  render thread and the GPU's front end. Keep the full bind for a command list's first use (a previous submission's
  stages and handles are still there).
- **Vectorise byte swaps by hand.** GCC did not vectorise a 16-word load / swap / store loop over a constant group;
  one NEON load feeding a raw store and a `REV32` store did both copies.
- **Count why draws leave the fast path, then close the cheap reasons.** Banjo's fast path logs its misses by reason
  ("no state", "chain break", "targets/predication", ...). Two were cheap once looked at: a point-sprite draw (its
  constants in ring uploads) left no state for the next draw although the fast path already puts back everything such
  a draw changes (8,700 misses per 10 s on the title screen, 14 after); and a predication change sent the draw the
  full way although only the viewport reads the predication (now one of the viewport's inputs).
- **Size direct-mapped caches for the busiest scene, not the average.** The fast path's pipeline cache had 1,024
  slots for a city frame of ~3,200 pipelines: 7.5 % of the draws rebuilt their key every frame.
- **Invalidate only what can dangle.** Banjo voided every geometry object's answer whenever any cache entry was
  erased; the answers only ever hold whole-buffer entries, and the erases were mostly range copies, so 30-40 % of the
  city's draws re-did their lookups while driving. Mark what the long-lived pointers can hold and void them only when
  one of those goes.
- **Watch the heap's floor, not just its peak.** On a 3 GB heap that ends in 260,000 free pieces, every allocator that
  keeps a minimum (a ring's first chunk, a pool's chunk that one live copy pins, pipelines built ahead from a list that
  holds every level) is memory the next big block cannot have. Banjo halved its ring and pool chunks, and when the
  heap's room (what sbrk has not handed out) falls below 256 MB it gives back prebuilt pipelines no draw has taken for
  3 minutes, drops idle geometry copies sooner and frees its spare command buffers. Not before: given back with room
  to spare, a scene visited again rebuilt hundreds of pipelines at once.
- **Retake the PGO profile after a rewrite.** A function written or changed after the profile is compiled without it
  (GCC's guessed profile), so a profile older than the renderer leaves the hottest code unprofiled. Banjo's release
  build used a 10-22 profile until the night of 10-28, while everything of 4.1-4.6g came after it. Ninja does not see
  the profile files: apply a new profile with a clean build (docs/pgo-lto.md).
- **Never walk the heap from a periodic report.** newlib's `mallinfo()` walks every free piece with the malloc lock
  held. Banjo's memory report called it every 30 s on the render thread: 25-46 ms in the city (150,000-260,000 free
  pieces), a stall for the render thread and for every thread that allocated meanwhile. Print the room sbrk has left
  (O(1)) and walk rarely, timing the walk.
- **No file-system calls on the render thread.** A list of pipelines appended with a flush per record makes a call
  into the file system service for each new pipeline (hundreds in a row in a new area), and its once-a-second fsync
  is milliseconds of the card's time. Append the encoded records to a buffer and let a background thread write, flush
  and sync them every second.
- **Do not let deko3d abort on a full heap.** deko3d takes a memory block's storage from the heap (aligned_alloc,
  then zeroes it) and aborts the process when that fails: no crash handler runs, the log just stops. Try the
  allocation first (memalign of the same size and DK_MEMBLOCK_ALIGNMENT, freed at once); on failure release what
  you can and try again, and if there is still no room log it and return no block. Do not hand deko3d storage of
  your own instead: it does not zero such a block, and the zero vertex buffer and command memory rely on the
  zeroing (Banjo's emulator run hung that way). And give back image-heap chunks that empty: a 64 MB chunk kept
  forever turns a texture peak into the heap's high-water mark (Banjo's crash: 1.2 GB of textures in one area,
  then a block that did not fit).
- **Make that failure path cheap, and make pools back off.** Banjo's first version logged the heap's free figures
  (`mallinfo`) at every failure: with 170,000 free pieces that walk took about 20 ms under malloc's lock (the audio
  threads waited in `free`), and a stream-cache pool that asked for a new 32 MB chunk for every new stream turned the
  out-of-memory point into a 95 s freeze at 0.1 fps (3,812 failures). Log the figures for the first few failures
  only, fail a repeat of the same request at once for a short time, and have every growing pool stop asking for a
  while after a failure (its callers need an uncached path anyway).
- **Give GPU memory its own region at the heap's top.** deko3d (and libdrm_nouveau, see battd_nx's bp_gpuarena.c)
  takes big page-aligned blocks from the same newlib heap as every small allocation; freed ones get cut up, and the
  heap ends with hundreds of MB free and no 32 MB run. Banjo's fix (h10, plume_deko3d.cpp "The GPU region"): pass
  deko3d your own storage (`DkMemBlockMaker::storage`, zeroed and flushed by you) from a region that grows down from
  the heap's end by lowering `fake_heap_end` under `__malloc_lock` (devkitPro's `_sbrk_r` reads it at every call;
  check yours), packed at its top, its free bottom given back. Unlike a fixed arena it needs no size guessed; keep
  battd's rules: no logging under the region's lock, and test the allocator on the PC first
  (tools/test_gpu_region.cpp).
- **Once the GPU is the limit, check it really is before tuning it.** Banjo's night_final city: GPU busy 44.5 ms of a
  47 ms frame (summed command-list timestamps), render thread 31.7 ms of CPU, main thread 27.9 ms, cores 0-2 about
  82 % used: GPU-bound. Then look for exact GPU work in this order, with a counter before each change: copies a
  resolve makes that the game does not need (Banjo: the depth copies were needed: the game writes depth after the
  resolve, 263 of 264), barriers that order nothing (Banjo: none spare), the constant stream (Banjo: about 0.6 ms of
  front-end work, not worth it), anisotropy (Banjo's game asks 2:1 at most). Banjo's GPU time was the game's own
  pixel work; what was left was resolution.
- **Dynamic resolution through the scale codes.** If your shaders read the internal heights from a constant
  (Banjo: SharedConstants v[84].w, handheld | docked << 16), give each mode a range with 720 at one end (Banjo: 540-720
  handheld, 720-1080 docked): the 720 end draws at 1x (code 0, the game's own targets) and the other end takes the
  mode's code with its height set once for the session (an end at the display's own size uses that fixed code: Banjo's
  1080 is code 2, 1.5x, with no HUD upscale). Switch codes between frames by the GPU's time: the same
  render-target switch the menus make, no shader change. Average the GPU time (about 16 frames), go down above a
  budget, come back when the low time times the pixel ratio fits under 85 % of it, and hold each change for seconds.
- **Key a cache of derived state by exactly what it is derived from.** Banjo's host draw packets (enhancements-reference
  "Host draw packets") first keyed a draw's derived state by the whole device state: the same state came back for
  5.6 % of the draws, because the vertex streams, index buffer, vertex fetch constants and texture words change every
  frame while what the packet holds never reads them. Keyed by only the words the answers read (from the device's
  field tables), 64.9 %. Measure with a census before building: a key per field left out shows which field churns.
  Then measure the cache itself on the console: Banjo's packets came out neutral (-0.54 and +0.45 ms a frame in two
  runs) and are OFF by default. A 1.6 MB table on an A57 with a 2 MB L2 evicts about what it saves.
- **Share host objects made from content alone.** When the game makes a guest object per mesh for the same shader,
  give the live objects one host shader (a registry by cache entry with a count), not one each: each made its own on
  the render thread (unpack, copy, parse the GLSL), kept its own text, and rebuilt and hashed each variant's text to
  find it in the device's variant table (the host's memo of its variants is per host). Keep the record's field a raw
  pointer if your records' cache-line layout was tuned.

### 4.7 Record-time extents for memexport checks
`banjo_renut_record_extents` (A/B 43). With exports pending at nearly every draw, each sampled texture's guest extent
is worked out on the recording thread; the render thread tests it against the 1 KB export bitmap. Only for games with
memexport.

### 4.8 Four cores (`banjo_system_core`, NetherSX2_nx's layout)
With a forwarder whose NPDM grants CPU 0-3, core 3 takes the audio output, the Audio Worker and XAudio's mixer threads
(the mixer at 0x2D); everything else stays on 0-2 (libnx's `pthread_create` gives the whole mask: `--wrap` of
`svcSetThreadCoreMask` narrows it). Banjo writes its forwarder's NPDM itself (`sdmc:/atmosphere/contents/<id>/exefs/
main.npdm`, `banjo_forwarder_program_id`). The system uses about half of core 3: put only what fits (Banjo's first try
with compiles and the XMA decoder there stuttered). `docs/four-core-mode.md`.

### 4.9 Audio
Native mixer pieces (voice filter, reverb, sample-rate converter, ramped mix), the XMA frame cache
(`banjo_xma_frame_cache`: a sound played again from its start reuses its decoded frames; the codec catches up on
skipped frames before the first miss). Port the cache as is (it is in the SDK's XMA context).

### 4.10 Optional, not pixel-identical
Shadow-caster culling (`banjo_renut_shadow_cull_indices`, A/B 44 to compare in one session): depth-only draws of at
most N indices into square depth targets. The user decides per game.

## 5. Measuring

- Console, stock clocks, a **static** spot (driving changes the load); `renut.toml` for 2 minutes, then
  `renut_ab.toml` (the targets of the round) for 10+ minutes. Frame rate follows 1000 / render-thread ms when the
  render thread is the limit: watch that first.
- **Then check whether the cores are full** (Banjo, 2026-10-28). Sum the game threads' CPU a frame from the per-thread
  report: when the game cores are all busy (Banjo's three were about 98 % at 51 ms a frame: render thread 47 ms, main
  28, recording 47, others 25), the frame is the total divided by the cores, and a render-thread cut gives only part of
  itself back (A/B 48: -4.6 ms render thread, about -2 ms a frame). Moving work from the render thread to the recording
  threads is then worth nothing unless it gets cheaper on the way. Judge items by CPU a frame of all threads (the A/B
  halves report it) and fps.
- Emulator (patched Ryujinx): title screen only, every verify key on; it proves exactness, not speed (its GPU timer
  and timing are not the console's).
- Profiles: `rex_profile.log`'s per-thread samples; `rt_lines` style mapping of render-thread samples to source lines
  with addr2line (keep the ELF of every package).
- Cache-miss audit (Banjo, 2026-10-28): list the hottest sampled instructions with the one before and the source line
  (objdump -dl). On the A57 nearly all of them are loads that missed. For each, find the structure and the field offset
  in the load (`ldr x2, [x2, #32]`: the field 32 bytes into the object), then either move the field to a line the code
  already fetched, or prefetch it a stage earlier where its address is known (Banjo's replay: an op's record two ahead,
  its objects one ahead, the copies they name at the op). Banjo's first two audits found about 15 such loads at 0.2 to
  1.5 % of the render thread each.

## 6. Pitfalls (each cost Banjo a build or a run)

- A patch that rewrites an expression can rewrite its own declaration (`const int ab = ab;`); GCC is silent without
  optimiser warnings. The syntax checker lints for it.
- Globals declared in a header but defined inside a .cpp's anonymous namespace: undefined reference at link.
- An always-inline helper that builds a large temporary puts it in every caller's stack frame (25 KB `Bound{}`).
- Editing a header while a build compiles mixes layouts. Stopping a build's shell on Windows leaves ninja / ld running
  and their children failing.
- The A/B target list was silently capped below 32 once; check that every target prints its line.
- Cvars read in `main` are the defaults: the toml loads later (`rex_app.cpp`), so read them again after `LoadConfig`.
- devkitPro's current `npdmtool` wants `program_id` keys (`title_id` is ignored: id 0).
- Sorting draws (Banjo's shadow sort) can be exact and still lose: the game's own order is near front-to-back for the
  GPU, and the sorted order reads state from all over the recording. Measure on the console before keeping a reorder.
- A look-ahead that peeks at the next op's words cost more on the A57 than the misses it hid (+2.15 ms).
- Thread placement on the system core: the system takes about half of it; audio there must leave headroom.

### 6b. Measured SLOWER ON THE CONSOLE (Banjo keeps them off; port them off or not at all)

Each is exact and has its key and A/B target, so another game can measure it again. Each made Banjo's frame slower on
the A57 in the city (render thread ms a frame, on minus off). The code marks each with `SLOWER ON THE CONSOLE` at its
key, so grep for that.

| Key (default off) | A/B | What it does | Measured |
|---|---|---|---|
| `banjo_renut_replay_lookahead` | 29 | the replay peeks at the next op's words to prefetch its shader lookups, headers, memo entries | +2.15 ms (2026-10-27); +2.79 ms in the 30 s check |
| `banjo_renut_shadow_sort` | 40 | depth-only runs replayed sorted by shader | +1.13 ms render thread, +0.65 ms GPU (2026-10-27) |
| `banjo_renut_texture_slot_skip` | 53 | a slot whose texture object answered last keeps its words without FillTextures | +0.64 ms (night_final); +0.69 ms in the 30 s check |
| `banjo_renut_inline_line_compare` | - | inline loads instead of memcmp per shared-constant line | PushSharedConstants 2.7 -> 4.9 % (36q); no effect since 2026-10-23 |

What they have in common: each adds a check or a load to every draw to skip work that was already cheap on the A57,
or that the draw's own prefetches had already covered. A census on the emulator (x86 caches) called each of them a
win. Measure on the console before keeping a "skip if unchanged" item.

Mixed in night_gpu4's two runs, to be measured again: 59 (point-sprite fast state), 46 (replay stream), 27 (setters'
tags apart).

Cleared by the 30 s check (night_gpu5, four-core, a no-op target as the control): 34 resident literals (-0.34 ms, on)
and 57 record prefetch (-0.21 ms, no effect). Both had been marked slower on 15 s samples. Measure with 30 s halves or
more, and keep a target that does nothing on your platform in the list as the control: Banjo's target 1 (a Vulkan-only
key) measured +1.8 and +1.3 ms in two 15 s pairs.

## 7. Still open in Banjo (so you know where the ceiling is)

- The render thread is the limit (~50-57 ms in the city, 100 % of its core). Next in Banjo: splitting it across two
  cores (front: replay, state, resources; back: deko3d recording), and hooking N&B's own loaders (Phase B).
- The GPU at ~40 ms (shadow maps, post-processing).
- 2026-10-28 (per-draw night): after the record-time objects and two cache-miss audits, the render thread's profile is
  flat: its largest function (the fast draw) is 21 % of the thread spread over ~150 source lines, nothing above 1 %.
  What still paid came from work done for nothing (a sweep that cleared answers it did not need to, a cache walk every
  frame for 0.1 MB, a whole-state invalidation per destroyed record, an unvectorised swap loop, both shader stages
  rebound when one changed). Look for those before more prefetching. Also: cores 0-2 are full and the system takes
  about half of core 3, so moving threads gains nothing; only less work does.
- The exact GPU items with a clear payoff (depth-only vertex shaders, early-Z, no pixel shader for depth-only draws)
  are in; what is left (the water's pixel shader, the shadow passes' per-draw front-end cost, resolves) needs console
  timing to judge. On Banjo the CPU sets the frame rate first (render thread ~52 ms against ~41 ms of GPU).
- Late on 10-28: the render thread's periodic work and the per-object work turned out to hide more than the per-draw
  code: a heap walk every 30 s (25-46 ms), a host shader made for every shader object of the same shader, a
  file-system call for every new pipeline. Look at what runs every N seconds and at what runs once per guest object
  when the game makes objects per mesh. Open in Banjo: the record-time objects are never given back (a limit for very
  long sessions); the memexport seeds' copy-engine copies and full barriers (needs a hardware check of the GPU L2
  against CPU writes). (The fast path's direct-mapped pipeline cache was checked: its misses are all conflicts, about
  2 % of lookups after the 4,096 entries and the epoch rule; not worth a two-way table.)
