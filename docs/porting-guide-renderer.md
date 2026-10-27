# Converting a ReXGlue Switch port to Banjo's renderer (2026-10-27)

For the other ports (SA2, Sonic Transformed, Marathon-style titles, futari, bejeweled, Mass Effect, ...) that want
what banjo-nx's renderer does today. It follows on from three earlier guides, which still hold for the first steps:
`docs/rexglue-api-renderer-guide.md` (why an API-level renderer, Unleashed's and Marathon's model),
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
group, with shader literal residency). Port as is; the register layout is the device's.

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
- Emulator (patched Ryujinx): title screen only, every verify key on; it proves exactness, not speed (its GPU timer
  and timing are not the console's).
- Profiles: `rex_profile.log`'s per-thread samples; `rt_lines` style mapping of render-thread samples to source lines
  with addr2line (keep the ELF of every package).

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

## 7. Still open in Banjo (so you know where the ceiling is)

- The render thread is the limit (~50-57 ms in the city, 100 % of its core). Next in Banjo: splitting it across two
  cores (front: replay, state, resources; back: deko3d recording), and hooking N&B's own loaders (Phase B).
- The GPU at ~40 ms (shadow maps, post-processing).
