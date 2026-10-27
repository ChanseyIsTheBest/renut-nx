# The record-time draw path (2026-10-26, the user's mandate)

The user's call after the stages_gpu1 console log (city, stock clocks: 14.5 fps, the render thread 66.6 ms a frame at
102 % of core 1, 12.8 us a draw over 5,200 draws, the same as batch 7): replace the per-draw re-derivation instead of
trimming it, Unleashed's model, "no half measures". This file is the plan and the record of the work; results go to
docs/deko3d-renderer.md as usual.

## Why trimming stopped paying

Each draw today: the game thread records the device's changed 64-byte chunks (from the setters' marks); the render
thread rebuilds the 12.7 KB device from them (ApplyState) and DrawStateLocked derives everything from it, about forty
steps a draw (shader and declaration lookups, prefetch loops, export checks, guest texture headers read, bools and
alpha, viewport, geometry, fetch data, constants, key and pipeline reuse checks, three uniform binds, write counters,
census hooks). The chain-reuse work made most steps cheap when nothing changed (targets reused for 99 % of draws,
textures 59 %, pipeline keys 38 %), but each step still runs its checks, reads its inputs and misses the A57's caches:
the console profile is flat (no category above 26 %), so removing one piece moves the cost elsewhere. The other cores
are busy too (the game threads 184 % of the remaining two cores), so the work cannot just move: it has to shrink.

## The design

**Layer 1: a lean executor, FastDrawLocked (render/fast_draw.cpp).** A persistent host state H: the bound pipeline and
its key, the targets, the texture and sampler words, the viewport, the pushed constants' tracking, the streams and the
fetch window. A draw redoes only the groups its recording's field bits (ChainHint::fields, from the setters' marks,
verified to miss nothing since batch 7) say changed; render-thread events invalidate their part through epochs (surfaces
built or retired, textures changed, prebuilt pipelines, a new command list, the frame). A draw outside the fast path's
cases (it reads a pending export, tessellation, Draw*UP / Begin*Vertices, the fur's skin-once or shells, point sprites,
the draw log, the census, a target redirect) goes to DrawStateLocked, which resets H. banjo_renut_fast_draw (A/B
target 25) switches it; banjo_renut_fast_draw_verify runs both and compares what they would record (the shared
constants block, the float constants pushed, the pipeline, the targets, viewport and scissor, the streams and index
buffer, the draw's arguments).

**Layer 2: record-time translation.** The recording side decodes the groups that changed into compact host fields as it
records (texture fetches, the render-state words MakeKey and ComputeViewport read, shader and declaration addresses,
bools, stream and index words), and FastDrawLocked reads those instead of the rebuilt device: ApplyState goes for the
fast path's draws. Inherited state of recorded command buffers is translated when the buffer is queued to run (the
running device is known then; every recording runs once).

## Stages

1. FastDrawLocked for the common case, with the verify, on the delta chain and the replays. Emulator: title-screen
   flyover and city route, verify 0 differences, then the console.
2. Widen the fast path until about 95 % of draws take it (the fallback reasons are counted in the 10 s line).
3. Layer 2, ApplyState gone for fast-path draws.
4. ARM work on what is left (the console profile decides).

## Census (banjo_renut_diag_fast_census, emulator, title-screen attract loop, 2026-10-26)

What changed from one draw to the next (the recording's field bits): the index buffer 84 %, fetch constants 72-77 %,
shaders 64.5 %, streams 64.5 %, texture words 42 %, view packets 9 %, targets 4-5 %, bools 4 %, declaration 2-3 %;
nothing at all 4-13 %. 98.5 % of the draws are replayed from recorded command buffers, chain breaks 1.2-1.7 %, Draw*UP
11.8 %, points 3.3 %, a memexport pending at 100 % of draws. Recordings against the same command buffer's the frame
before: 40 % of the recordings are the same, but they are the draw-less ones: 0-4 % of the draws are in a repeated
recording (the game records its draws anew each frame), so a cache of translated recordings across frames does not pay.
Consequence: most draws are a new mesh with a new material; the executor must make the shader, texture, stream and
index changes cheap, not just skip what did not change (the rare groups are viewport, targets, bools, declaration).

## Stage 1 as built (FastDrawLocked, render/draw.cpp)

- g_fast (FastState): the shaders, declaration, targets, viewport, depth-only answer, alpha test, texture slots (mask,
  header words, bind effects), pipeline key and pipeline, export answer, profile label of the last draw. DrawStateLocked
  stores it at its end (StoreFastStateLocked) unless the draw leaves something its next one undoes (skin mode, points,
  memexport) or is one the fast path never draws.
- Gates: textures on kFieldFetch / kFieldTexWords / shaders / depth-only / the texture frame and epochs / the slots' guest
  headers; bools and alpha on kFieldBools / kFieldViewPackets / shaders; the viewport on kFieldViewPackets / kField29C0 /
  the tiles / the replay's predication; the key on kFieldBools / kFieldDest / kFieldViewPackets / shaders / declaration /
  primitive / instancing / alpha / A/B half; the pipeline on the key and the prebuild and lookup epochs. Per draw: whole
  geometry (a draw that cannot be whole goes back), bound fetches and fetch data, float and shared constants, binds.
- The fallback reasons are counted in the "[renut] fast draws" 10 s line; DestroyShaderRecord and
  DestroyVertexDeclarationRecord drop g_fast (it holds their records).

## Stages 1 and 2, emulator results (title-screen attract loop, verify on)

- v1 (the device declaration only, no continuing after unusual draws): 0 verify differences, 21-24 % of draws fast;
  most went back because their shaders changed with a shader-bound declaration (D3DVertexShader_Bind or the shader's
  own fetches).
- v2 (the declaration derived as DrawStateLocked does; continuing after Draw*UP, range, point, memexport and skinned
  draws, putting back the skin mode, point state and memexport windows they leave; the pipeline cache): 0
  differences, 78-86 % fast. The pipeline cache (FastPipeline: render-state epoch, shader entries, layout, primitive,
  instancing, alpha test, A/B half; 512 entries) answered about half the key changes.
- Stage 2 (targets gathered again in the executor with DropSmallerDepth, a state with the depth target left out kept
  while the depth control does not change, range geometry and Draw*UP in the executor, rectangle lists): 0
  differences, 92-95 % of draws fast in the 3D scenes (about 53 % on 2D screens of a few hundred draws). Still
  DrawStateLocked's: chain breaks, points (their pipelines take the point-sprite geometry shader, not the pushed
  constants), tessellation, the composite, the predication, the fur family, a pending export's window read.
- Package builds/2026-10-26_2203_fastdraw1 (NRO 51858669fb7c): renut.toml (the executor on), renut_ab_stages.toml
  (A/B targets 25, 22, 23, 24, 20, 21: 25 = banjo_renut_fast_draw first), renut_steps_off.toml (every switch since
  batch 7 off, the executor too).

## Console, fastdraw1 (renut.toml, the city, stock clocks)

17.4 fps (stages_gpu1: 14.5), the render thread 55.2 ms a frame (66.6), 11.5 us a draw over about 4,780 draws (12.8),
the GPU 38-39 ms; 97 % of the draws fast. The profile (1,064 samples): FastDrawLocked's own lines 13.4 %,
WholeGeometryLocked 10.4 %, FillTextures 10.4 %, PushFloatConstants 7.0 %, memcpy 6.0 %, the replay loop 4.5 %,
DrawNeedsExportFlush 3.6 %, PagesClean 2.6 %, ApplyState 2.2 %, UploadVertexFetchData 2.2 %. The lines: cache misses
(the memo slots and map nodes of the stream and index caches, the copies' proof fields, the pipeline cache's entries, the
shader entries, the guest texture headers): the old path's prefetches had no counterpart in the executor.

## Rounds 3 and 4 (2026-10-26 night)

- Round 4 (ARM): WholeGeometryTry in three passes (the keys and checks with each last copy's entry or memo slot
  fetched, then the entries the slots name, then the lookups), the index buffer's header fetched first; the executor
  fetches the shader entries when the shaders change, the texture headers and memo entries as it converts the fetch
  constants (or the kept slots' headers before their compare), the pipeline cache's likely entry; the memexport
  buffer's address asked only when the buffer changes; the per-shader profile label made only when its inputs change.
- Round 3 (lookahead): the replay loop fetches the next draw's texture memo entries (the slots this draw bound, from the
  next op's texture words and fetch constants: PrefetchNextTextures); DrawNeedsExportFlush's texture answers kept by the
  fetch words (512 entries) instead of by the slot (the slot's answer was another texture's at nearly every draw).

## Rounds 3 and 4: the ARM pass over the new code (2026-10-26 night, the user's request)

- One guard a draw: the executor's switches read into one static (four function statics before).
- g_layout_epoch (render/textures.h): raised by every layout change the three Transition*Locked functions make. With
  the texture frame and epochs and the surface epoch it lets the executor skip (a) the unchanged textures' transitions
  (each bound texture's layout read: a miss a slot), keeping the bind counters' shares from the fill, and (b)
  BindTargets while the framebuffer it bound is still bound, no layout or framebuffer epoch moved, no depth target is
  handed over and the profile does not mark per target.
- The export test by kind (FastExportReads): streams, fetches by address, the index buffer (its guest header read only
  then), the textures, each again only when its inputs or the pending exports changed; the texture fetches converted
  only for the fill or the textures' test.
- The bound fetch entries and the fetch data's words compared with their last values and marked only where they
  changed: marked unconditionally, their shared-constant lines went to the GPU again at every draw.
- The geometry's last-copy entries fetched two lines (the proof fields span both).
- PendingExportSlotsMemo backed by an answer table by range (512 entries) behind the caller's slot.
- FillTextures' slot-last path (the most common hit): the transitions only when g_layout_epoch moved since they last
  held for the slot (SlotLast::layout_epoch).
- Stages 1-2 (the user's follow-up): the shaders' own cases (exports, composite, skin-once, fur) and the texture mask
  read only when the shaders (or the depth-only answer) change, the kept shaders' answer otherwise (FastState
  shaders_fast, draw_texture_mask; DrawStateLocked's stored state gets them too); the replay loop prefetches the next
  op's shader records only when it changed a shader word.

## How much of N&B goes through hookable D3D functions (2026-10-26 night, the user's question)

State census (banjo_renut_diag_fast_census, StateSourceReport in guest/command_buffers.cpp; NRO c9a5f4235bfd,
emulator title-screen attract loop, 16 reports): every recorded op was compared from the setters' marks (100 %, 0
chunks changed without a mark). 13,289,660 changed chunks were marked by hooked setters, 13,456 only by an inline
write's pending tag: 0.10 % inline.

| Field group | Inline share |
|---|---|
| fetch, bools, dest, decl, tail, index, targets, streams, tex words, scissor, shaders, predication | 0 % |
| private | 0.38 % |
| view packets | 1.98 % |
| misc | 6.8 % |
| 29C0 (HalfPixelOffset, state 81, written inline by the game) | 91.6 % |

- Hooked: every SetRenderState_* and SetSamplerState_* setter, SetTexture (and _Inline), SetStreamSource, SetIndices,
  SetVertexDeclaration, shaders, viewport, scissor, targets, predication, bool/int constants, every draw and clear,
  command buffer begin/end/run.
- Not hooked: float constants (the XDK inlines SetVertexShaderConstantF/SetPixelShaderConstantF; they arrive as
  m_Pending[0]/[1] bits, which the recording already reads).
- Setters re-set unchanged state: 22.3 chunks marked an op, 4.9 changed (about 4.5 redundant marks per real change).
  A setter-level translator must compare on set (Unleashed does) or it pays for all 22.
- Recording is per frame, not record-once: ops recorded are about two thirds of the draws replayed, so translation at
  set time runs every frame (as Unleashed's does).
- Title screen: 22 % immediate draws (a whole 12 KB device copy each); the city 1.5 %.
- Resources: textures and buffers are mostly built outside the Create hooks (earlier census: 72 % of textures, 99 %
  of buffers), but every use passes a hooked setter with the object pointer. Textures can take an object-keyed side
  table (host texture made at first bind, checked by the header words). Buffers cannot copy Unleashed: the game
  writes their memory without Lock/Unlock, so no hook says when contents change; the store-card proofs stay.

What a setter-level (Unleashed-style) translator would remove, from the rounds 3/4 console render thread (rt_r34,
1,046 samples, 64 ms a frame, 5,490 draws):

| Part | Share | With setter-level translation |
|---|---|---|
| decoding Xenos state | 14.8 % | moves to set time (compare-on-set), mostly gone from the render thread |
| FastDrawLocked checks | 12.8 % | mostly gone (dirty bits from setters instead of word compares) |
| snapshot replay rebuild + big-endian reads | 12.2 % | about two thirds gone (host-state deltas) |
| shader lookups (GetHostShader, FindShaderLocked) | 3.4 % | gone (record pointer at SetVertexShader/SetPixelShader) |
| whole buffers + proofs + geometry | 15.7 % | stays (no Lock signal for game-built buffers) |
| constants push | 9.7 % | stays |
| deko3d command recording | 11.0 % | stays (Unleashed pays it too) |
| memcpy and the rest | about 20 % | mostly stays |

Estimate: about 30-35 % of the render thread (64 -> about 42-45 ms), part of it moved onto the recording threads
(game threads, which are saturated too). It does not lift the measured ceiling: the game's own threads (about 106 ms
of CPU a frame on 3 cores) cap a perfect render thread at about 18-19 fps at stock clocks.

## Setter rewrite, batches 1-2 (2026-10-27, the user's "do this and more, be aggressive")

Console rounds 3/4 profile (rex_profile (22), city, 15.5 fps, 305 % CPU on 3 cores): render thread 100 % of core 1;
the game's two draw threads 61 % (their own submission code about 60 %, our recording 26-33 % of each: ChangedChunksMarked,
NewOp, MixedChunkFields, CopyState); audio 41 % (XAudio thread 28 %, XMA 9 %, worker 4 %); game logic 86 %; the uam
compile thread 9-16 % (a handful of new variants met in the city, not steady). The render thread's profile is flat
(no line above 2 %): FastDrawLocked's own work 17 %, geometry 14 %, textures 12 %, constants 10 %, replay 9 %, deko3d 9 %.

- banjo_renut_touch_compare (A/B 26, on): Device::SetW/SetH/SetB/SetQ store and mark only a changed value
  (compare-on-set). Touch marks an op 4.6-5.0 (changed 4.4-4.8).
- banjo_renut_tags_apart (A/B 27, on): a recording device's setters keep their dirty tags (m_Pending 2-4) out of the
  device, so the state tags (16 chunks each op) stand for the game's inline writes only. Marks an op 21 -> 7.2-8.4
  (4.6 touches, 2.6-3.2 inline tags). Emulator check with banjo_renut_tags_check = 1 (every op compared whole):
  190,464 ops, 0 chunks changed without a mark. A miss in normal play turns tags-apart off first (NoteUnmarked).
- Bulk copies refreshed (tools/switch_hot_bulk.py over the 16 console profiles since 10-21, --min-samples 8): 584
  listed, 580 made (363 before). Guard run (banjo_hot_guard_calls 64 / period 4096): 347 reached, 29,534 calls compared,
  0 differences. NRO 69 -> 72.6 MB. Not localizable yet: sub_82BDAED0 (2.5 % of the game threads, a 29-way switch
  interpreter: switch_goto.py's form), sub_82948928, sub_829A1D88.
- Per-op repeat census (RepeatCensusReport "by op"): 44-63 % of the draws are the same op as the frame before's at their
  place, but only 0.1-6.4 % have every op before them the same (the draw order moves), so a cache of translated
  recordings across frames does not pay; the content-keyed memos (textures, streams, shaders) are the right form.

Package builds/2026-10-27_0033_setters1 (NRO f33f4674b880): renut.toml, renut_ab.toml (A/B 27, 26),
renut_bulk_off.toml (the 580 copies off).

## Render thread night (2026-10-27, 02:00-05:00, the user's "work until 5 am, no console tests")

The setters2 console run (render thread 57 ms, 11.2 us a draw, 5,060 draws a frame): the GPU profile's passes put the
two shadow maps at 1,509 + 699 draws a frame (768x768 and 1024x1024 depth, no pixel shader), 44 % of the draws.
Per-mesh lookups dominate every draw (index, fetch and stream fields change at 64-84 % of draws), so a dedicated shadow
path cannot skip them; the night's changes cut what is left (all exact, each with an A/B target):

- Latch(): a plain load of g_latched_ready (an LDAR at each switch a draw read: 0.3-0.4 % of the render thread at each
  site); LatchTuning at OnRenderThreadStart.
- ShaderRecord::DrawSummary::skin_once (a binary search at every draw before).
- PendingExportSlotsMemo answers "none" from the 1 KB granule bitmap before its answer tables (their misses were 2 %).
- A/B 30: base-vertex draws for vertex shaders whose GLSL never names gl_VertexID (653 of the 945): whole copies stay
  bound at their start, no vertex buffer bind and no shared constant line a draw (plume Deko3DShaderReadsVertexId).
- A/B 31: a draw that samples nothing skips the texture refill (the slots' words stay); no pixel shader, no pixel
  constants pushed (the replay's dirty groups stay set for the next).
- Texture memo 4,096 entries; A/B 32: an entry of an earlier frame (same epoch and words) runs only the frame's content
  check (RefreshBindingLocked; 340 binds a frame went through BindTextureLocked for it).
- A/B 33 (banjo_renut_merge_draws): a replayed draw takes the following draws that change no state, write no constant
  and draw the next indices of the same list with the same base (one host draw, the same primitives in order); not for
  exporting, skin-once, fur or composite shaders.
- banjo_renut_diag_draw_kinds: draws by kind (shadow / other no pixel shader / colour): share, us a draw (op state and
  draw), fields changed, runs, and the merge census.

### Night results so far (emulator title-screen attract loop; console numbers pending)

- Census (banjo_renut_diag_draw_kinds): shadow draws 15-31 % of the title's draws (44 % in the city's GPU profile), runs
  of 12-18; between shadow draws the index buffer changes at 99 %, the vertex shader with its streams and vertex fetch
  constants at 66-75 %, the texture fetch constants at 0 % (after the field split). Emulator time a shadow draw about
  half a colour draw's.
- Merging (A/B 33): no draw is the draw before's state and constants again (each part has its own index buffer object):
  0 merges; banjo_renut_merge_draws defaults off.
- Base-vertex draws (A/B 30): 89 % of the whole-buffer draws (their vertex shaders never name gl_VertexID).
- Texture refresh (A/B 32) found nothing to do while g_texture_epoch moved every frame (hand-overs, resolve
  destinations): A/B 35 keys the memo by the page's and the address's epochs instead.
- kFieldVertexFetch (0x6F0-0x77F) apart from kFieldFetch: the vertex fetch constants of another mesh no longer make a
  draw take its textures again. (Vertex fetch indices 64-79 live in 0x680-0x6EF: the export and fetch tests read both.)
- Literal residency (A/B 34): about 1,000 literal pushes not made per 10 s; push-constants verify 0 wrong registers.
- Fetch copies made: 85-142 MB per 10 s on the title (FlushVertexFillsLocked's memcpy); A/B 36 keeps the draw before's
  fetch data when nothing it reads changed.
- Compiler: Latch() and its 36 accessors, GuestVirtualBase/GuestPhysicalBase and TextureFetch::FromGuest always inline;
  the texture slots' and render state's fixed-size compares as 8-byte loads (SameBytes; memcmp calls before).

### Night, later batches (b8-b10)

- A/B 35 verified (b8): the memo misses for "an earlier texture epoch" went from 8-12 k to about 0 per 10 s, the
  earlier-frame entries take the content check only (A/B 32: 24-28 k per 10 s), the slow binds about 85-90 % fewer than b6.
  Push-constants verify 0 wrong registers; fast-draw verify clean.
- A/B 36 (kept fetch data): about 18 k draws per 10 s on the title.
- b9: the A/B half read once per fast draw (7 loads of g_ab_off before); PrebuildEpochLocked relaxed; plume's
  currentEpoch() relaxed (a code load bumps codeEpoch before the variant is handed over under a lock).
- b10: A/B 37 (a stream's source as the draw before found it when its fetch words, buffer object, stride byte and shader
  are the same: no guest vertex buffer header read); A/B 38 (a whole index copy bound by its pool chunk, its place in
  the chunk given as the first index: one index binding for the draws of every copy in a 64 MB chunk).
- Looked at and left: the vertex buffers by chunk (the pool aligns to 256 bytes, not to strides); pipeline binds by
  dkCmdBufCallList (Ryujinx support unknown); the proofs once a frame (not exact); per-frame cross-op caches (draw order
  moves). The hot functions' calls were checked in the binary (objdump): the remaining calls are real work or cold.

### Night package and what is next

- builds/2026-10-27_0406_night1 (NRO a763e3bba91a, b10): every item above through A/B 38. renut.toml as setters2;
  renut_ab.toml (A/B 38-26, 30 s halves), renut_lean.toml (no per-shader GPU timestamps, no resolve trace),
  renut_census.toml (banjo_renut_diag_draw_kinds), renut_bulk_off.toml.
- b11: FastState keeps a pointer to the cached key (no 200-byte copy per key change) and FastPipeline::blend_used
  (DrawBlendConstant only for keys that read the constant); the open command list and its draw count handled inline.
- Ideas for later: sort a shadow pass's draws by vertex shader (depth-only with a less(-equal) test gives the same
  depth in any order) to cut its shader binds (72 % of shadow draws change the vertex shader); a layout epoch in the
  texture memo entries (no room in the 128-byte entry without narrowing 'frame'); the vertex buffers by pool chunk
  (needs stride-aligned spans).
- After the audit (06:00), what is left is structural, not exact micro work: (1) the shadow sort above, which needs
  each moved draw's state against its new predecessor (the ops hold deltas: a sort at record time, in NewOp, over runs of
  depth-only draws, writing their changed chunks against the new order); (2) drawing straight from the op's arena chunks
  instead of ApplyState's rebuilt device (the replay copies every changed chunk and constant group once more, about
  1-2 % of the render thread); (3) FindShaderLocked's last-shader answer inline (its memo is file-local in
  hooks/shader.cpp); (4) the A57 numbers of A/B 26-39 decide which items stay on. Smaller ones left: MixedChunkFields
  and ChangedChunksMarked both compare a mixed chunk (one pass could give both).

### Render thread audit (b11-b15, 2026-10-27 04:00-07:00, the user's "every last drop")

- Bug found by reading: b9's "A/B half read once" script replaced the load in its own declaration, leaving
  `const int ab = ab;` in FastDrawLocked (undefined). GCC took it as 0 in b9-b11 (checked in the b10 disassembly: no load
  of g_ab_off in the function), so every item stayed on and the pixels were right, but night1's in-run A/B could not turn
  off targets 25, 28, 30, 31, 36, 37 or 38 (their halves measure the same build). Fixed in b12. tools/switch_syntax_check.py
  (syntax-only compiles with the switch-dist flags from ninja, a minute, plus a lint for self-initialised locals: GCC
  warns about those only from its optimiser, not with -fsyntax-only) is run on each batch before the 13-35 minute build.
- D3DVertexShader_Bind on the render thread replaces the shader's bound declaration (freeing the old one) and strides:
  it now invalidates the fast path (which, like the stream memo, may hold both). N&B binds at creation, so it had not
  happened.
- Calls GCC kept out of line in FastDrawLocked (objdump of b10): DrawSummaryOf (a test and a return; now an inline
  read of the made summary, MakeDrawSummary cold), SyncBound, EnsurePushedBuffers, MemExportBufferLocked (it built a
  buffer description at every draw before finding the buffer big enough), GuestPtr / GuestPhysicalPtr (always_inline),
  GetHostShader at each shader change (HostShaderOf reads the built host shader inline), and memcmp for 16 and 32
  bytes (the bound fetch entries at every draw, SetShared's float4 / u32[4] / bools, the render state intern table, the
  blend constant): SameBytes now (moved ahead of SetShared).
- The replay's texture look-ahead skips the slots whose texture word and fetch constant the next op leaves as they are
  (seven words peeked and a memo entry hashed for each before; this draw's own lookup brings that entry in).
- A/B 39 (banjo_deko3d_texture_skip): plume's bindTextures read every binding's texture and sampler words from the shared
  constants and compared the handles at every colour draw. Each pipeline now has the 64-byte lines its bindings' words
  sit in (texLines); the renderer reports the lines each shared-constants push wrote into the copy plume reads
  (Deko3DSharedLinesWritten); a draw with the pipeline, descriptor epoch, image set and shared buffer of the last whole
  run and none of its lines written binds and pushes nothing, as the whole run would.
- b13: the fast path binds the three pushed uniform buffers (bindings 0-2) once per list, again only after
  BindConstantSet or a layout bind took a binding (plume found them bound already at every later draw: three calls a
  draw); PushFloatConstants byte-swaps each group as it copies it into the update (a second pass over the run before).
- b13 also: ResetBound (see the frame pitfall below); its link was cut short, so b14 carries it. b14: the deko3d report
  line counts bindTextures' whole runs and the draws A/B 39 skipped ("bound textures N read, M skipped").
- b12 and b14 verified on the emulator's title attract loop (fast_draw_verify, push_constants_verify, draw-kind census:
  no warnings; fast-path rates as b10, 92-95 %; pixels right). A/B 39 skipped 31 % of bindTextures calls (1.85 M of
  5.96 M). b14's FastDrawLocked loads the A/B half again (objdump), its frame is 3,456 bytes as in b10.
- Package builds/2026-10-27_0537_night2 (NRO a54f26caa448, b14): every item of the night, A/B targets 39-26 in
  renut_ab.toml; supersedes night1 (a note is in that folder).
- b15: NewOp's memo (the device's command buffer and recording set) kept per recording device by its g_touch slot
  instead of per thread (a thread_local is a call on the Switch, at every op of the draw threads); NextChainSeq inline
  (a call into draw.cpp at every replayed draw). Devices without a slot keep the thread's memo.
- b15 verified as b14 (no verify warnings, 0 chunks changed without a mark, pixels right; NewOp's per-op
  __aarch64_read_tp call gone, one left in TouchSlotOf's slow path). Package builds/2026-10-27_0606_night3 (NRO
  9607b3e518ad): the latest; night2 is fine but superseded (notes in both older folders).

## Shadow sort (2026-10-27 morning, the user's "start the shadow sort now")

banjo_renut_shadow_sort (default on, A/B 40; guest/command_buffers.cpp). A depth-only draw leaves the same depth target
in any order when each sample keeps the nearest (or farthest) value written: no colour written, no stencil, a less /
less-equal (or greater / greater-equal) test. The game's shadow passes are such draws, and 68-72 % of them change the
vertex shader from the draw before; sorted by shader they change it far less often.

- Record time (RecordDraw, SortFacts, the draw threads): from the device as the op records it: a depth target, stencil
  off, no colour target with channels written, the depth test's family (min, max, or no depth write), no Draw*UP /
  tessellation / point sprites. The op keeps its key (vertex shader << 32 | pixel shader, then the declaration) and its
  run's signature (the targets and the family).
- Queue time (PlanSortedRuns, the thread that runs the buffer, once a recording): runs of at least 3 consecutive draws
  with one signature, from op 1 on. The device of each op is the versions (arena copies) of every chunk and constant
  group; a run's ops write a few slots (W). Sorted stably by key; each sorted draw gets the W slots whose version differs
  from what the draw before it (in the sorted order) left, plus the constant groups whose source changes (the op's state
  or the running device: Op::vs_set / ps_set), so the pushed-constant model stays exact; a last entry brings the device
  back to the run's last op as recorded, from which the next op's own delta goes on.
- Replay (render thread): at a run's first op, each distinct key's shaders are checked once (SortableShadersLocked: both
  known, no memexport, no skin-once, not the fur family, not a composite pixel shader); the entries are applied
  (ApplySortPair) and drawn, the ops as recorded otherwise. The chain hints and dirty groups come from the entries.
- banjo_renut_shadow_sort_verify: each sorted run is also applied as recorded and the device of every sorted draw (and
  of the run's end) is compared whole with its op's; differences are logged and counted.
- The 10 s "[renut] shadow sort" line: draws sortable and why not, runs by length and what ended them, shader changes
  before and after, runs replayed sorted or refused.
- b16 (keyed by the shaders' guest addresses), emulator title loop, 10 s of attract play: 257,955 draws, 33.2 %
  sortable (not: stencil 148,695, colour written 21,718, the call 1,940); 388 runs, all longer than 64 draws (85,602
  draws, about 220 a run), every one ended by a clear or resolve; verify: 86,016 devices compared, 0 differ. But the
  shader changes went only 64,721 -> 60,381: N&B makes a shader object per mesh, so the addresses differ where the
  shader does not (about 155 addresses in a run of 220).
- b17: the key is the shaders' contents. Where a shader object is made (CreateShaderRecord, RegisterGuestShader, on
  the game's threads), its address, a 31-bit digest of its hash and whether its draws may be reordered
  (ShaderSortableByHash: the cache entry known, no memexport, not skin-once / fur / composite) go into a lock-free
  table (NoteShaderSortKey); SortFacts reads it (a draw of an unknown or unsortable shader joins no run). The replay
  still checks each distinct key's shaders once (SortableShadersLocked by the op's addresses). The key only orders:
  a digest collision or a table miss sorts worse, never wrongly.
- b17 (content keys): shader changes in the sorted runs 61,673 -> 18,997 and 48,632 -> 14,245 (-70 %) a 10 s window;
  pipeline binds 0.59 -> 0.44 a draw over the whole frame; verify 0 differ. b18 adds a second sort level, a digest
  of the render state the pipeline is made from (FastRenderState's words, the bools, the primitive).
- b19, A/B 40 with 30 s halves on the emulator: the sorted halves cost +27 % render thread (+32 % in busy periods).
  Cause: an entry's fields were whole-chunk, and the targets share a chunk with the index buffer and the streams, so
  nearly every sorted draw told the fast path its targets had changed (targets gathered again, viewport, framebuffer
  bound again). b21: the mixed chunks' fields word by word against the copy the device holds (as NewOp does), and the
  next entry's guest headers prefetched from its chunks (PrefetchSortedHeaders, the look-ahead the recorded path has).
  b21, A/B 40 with 30 s halves: +0.6 % over all periods, -2.7 % in busy ones (the emulator's noise); shader changes
  1,972,714 -> 591,918 over the run. One b21 run with 5 s halves stopped presenting at the first gameplay scene (62 s);
  the 30 s run did not.
- A repeat of the 5 s run did not stall (an emulator flake, as far as two runs tell); its paired halves (49 periods):
  sorted +5.1 % render thread. The census by half (b21, 30 s halves): a shadow draw 7.21 us sorted, 7.10 us as
  recorded; the shaders field still changes at 69 % of the sorted draws (each mesh has its own shader object, so the
  address changes where the contents do not), and the fast path took every address change as a shader change: the
  pipeline cache looked up again, only deko3d's bind skipped. (b19's coarse fields: 11.17 us sorted, 7.96 as recorded.)
- b22: the fast path's key test compares the shaders' cache entries and the layout hash with the key's own (MakeKey
  reads the shaders only through their entries and BoolSpecMask, the declaration through its layout hash), so another
  shader object of the same contents keeps the key and the pipeline (A/B 41 puts the address test back). For every draw,
  sorted or not; fast_draw_verify compares the key it would make at every draw.
- b22 (emulator): fast pipeline-cache lookups 0.66 -> 0.42 a fast draw; verify clean (3.57 M sorted devices compared,
  0 differ; no key differences). Census by whether the sort replayed in that report (each shadow-sort line labels the
  census line of its report): shadow draws 7.55 us sorted, 7.50 as recorded; A/B 40 over the run +1.6 % (busy +0.5 %).
  The sorted shadow draws changed the texture fetch constants and words at 21 % (0 % as recorded): a run's ops differ in
  leftover texture state between its early and late parts, one change as recorded, many when interleaved.
- b23: a third sort level, a digest of the first four texture slots' addresses (the leftover state groups again).
- b24: entries_changed (A/B 41 too): the depth-only answer, the texture mask, the texture slots, the export reads (and
  the declaration compared by pointer for the streams'), the pipeline-entry prefetch, the bools' answer and the kept
  fetch data now follow the shaders' cache entries, not their objects (only the lookups of the records themselves stay
  on the address change).
- b24 (emulator): verify clean (2.59 M sorted devices, 0 differ; no key, texture, constant warnings). Census by sort
  state: shadow draws 7.39 us sorted, 7.48 as recorded (-1.2 %; b22 +0.7 %). The sorted draws still change texture
  fetch words at 21 % (the texture-slot digest did not group them). The whole-run A/B said +19 % / +24 % for the sorted
  halves, but those halves held 233 shadow draws a frame against 108: with 30 s halves the A/B's 60 s cycle keeps step
  with the attract loop's scenes, so the halves are not the same work; the census's time a draw is the measure here.
  Package builds/2026-10-27_1047_shadowsort (NRO 7f7ca5bfb78e): sort on, A/B 41-26 in renut_ab.toml (the list fix
  lets 32-41 run). On the emulator the sort is about even on the render thread; the console decides (CPU per draw on
  the A57, and the GPU's fewer shader and pipeline switches).

## Console run of the shadowsort package (2026-10-27, the city, handheld, stock clocks)

builds/2026-10-27_1047_shadowsort (NRO 7f7ca5bfb78e): every item of the night, the shadow sort on, the A/B list fixed
(targets 32-41 run at last). Two runs: logsbaseline (renut.toml, 2 minutes) and logsab (renut_ab.toml, 21 minutes, A/B
41-26, 30 s halves; 26-30 got one period, the rest two). City windows only (render thread above 40 ms):

| | setters2 (2026-10-27 01:21) | baseline run | A/B run |
|---|---|---|---|
| fps | 16.1 | 17.2 | 15.9 |
| render thread | 56.9 ms | 53.6 ms | 56.4 ms |
| GPU | 41.0 ms | 40.5 ms | 41.5 ms |
| draws a frame | 5,060 | 5,220 | 5,585 |
| render thread a draw | 11.23 us | 10.28 us | 10.10 us |

The night's items cut the render thread's cost a draw by about 9-10 % (the A/B run's frames drew 10 % more, and its
halves switch items off). Still far from 33 ms.

In-run A/B (on minus off; negative: the item saves):

| A/B | item | periods | render on / off | render | GPU |
|---|---|---|---|---|---|
| 41 | shader checks by contents | 1 | 56.34 / 56.71 | -0.37 ms (-0.7 %) | -0.13 |
| 40 | shadow sort | 2 | 56.72 / 55.59 | +1.13 ms (+2.0 %) | +0.65 |
| 39 | texture-bind skip | 2 | 56.00 / 56.34 | -0.33 ms (-0.6 %) | -0.05 |
| 38 | index copies bound by chunk | 2 | 56.48 / 57.06 | -0.57 ms (-1.0 %) | -0.08 |
| 37 | kept stream sources | 2 | 56.30 / 56.26 | +0.04 ms | -0.05 |
| 36 | kept fetch data | 2 | 56.54 / 56.98 | -0.44 ms (-0.8 %) | -0.05 |
| 35 | texture epochs by page / address | 2 | 56.31 / 56.69 | -0.39 ms (-0.7 %) | -0.11 |
| 34 | resident literals | 2 | 56.41 / 56.84 | -0.43 ms (-0.8 %) | -0.13 |
| 32 | texture memo refresh | 2 | 56.30 / 57.24 | -0.93 ms (-1.6 %) | -0.01 |
| 31 | no texture / pixel work without a pixel shader | 2 | 56.97 / 56.99 | -0.02 ms | +0.12 |
| 30 | base-vertex draws | 1 | 56.34 / 56.29 | +0.05 ms | +0.11 |
| 29 | the replay's look-ahead prefetch | 1 | 55.88 / 53.73 | +2.15 ms (+4.0 %) | -0.12 |
| 28 | render-state numbers by content | 1 | 55.78 / 57.91 | -2.13 ms (-3.7 %) | -0.02 |
| 27 | setters' tags kept apart | 1 | 56.07 / 56.76 | -0.69 ms (-1.2 %) | -0.10 |
| 26 | setters mark only changed values | 1 | 56.07 / 56.16 | -0.09 ms | -0.21 |

Other counters of the A/B run: fast draws 97.8 %; A/B 39 skipped 2 % of bindTextures' calls in the city (31 % on the
emulator's title); the shadow sort found 35.5 % of the draws sortable, about 155 k a 10 s window refused by their
shaders, most runs ended by an unsortable draw, and the shader changes in its runs went 116 k -> 60 k (-48 %).

Decisions (the user's, 2026-10-27): the shadow sort (banjo_renut_shadow_sort) and the replay's look-ahead (now its own
key, banjo_renut_replay_lookahead) are OFF by default; each has a note at its definition (guest/command_buffers.cpp)
with these numbers. Why the sort lost on the console: the game's own order is close to front to back, which the depth
test's early rejection uses (the GPU's +0.65 ms); the city's runs are short; and each sorted draw reads its device
bytes from all over the run's arena where the recorded order reads one op's in a row. Why the look-ahead lost: on the
A57 its peeks (seven words a texture slot, the header chunks, a memo entry hashed a slot) cost more than the misses they
hide, which the draw's own prefetches mostly reach anyway. Both stay in the code for an A/B with their key on (targets
40 and 29); a sort that keeps the front-to-back order inside each shader's group was not tried. The next package's
renut_ab.toml measures 40, 29 and 28 with their keys on and more periods each.

## Mesh objects (#8, 2026-10-27 afternoon, the user's "work on 8 also 2, 3 and 4")

What #8 of the CPU-gap table asked for: render-thread resource objects with push invalidation, as Unleashed has (a
draw names host objects; nothing is looked up or checked per draw). What N&B allows, measured first: its draws name
resources by guest addresses it builds itself (the Unleashed census: 5.3 % of streams, 0.1 % of index buffers and
26.4 % of textures come from D3D creation calls), so a draw cannot carry a host pointer the way Sonic's do; the
address -> object step stays. The per-line profile of the shadowsort city run (render thread, 39,867 samples) put the
resolution's cost here: WholeGeometryTry 8.8 % (the stream memo's slot and entry 2.0 %, the copies' entries 1.5 %, the
index copy's proof 1.0 %, the keys 1.2 %), FillTextures 8.0 % (the slot's last answer and its epochs 1.8 %, writing the
slot's words 0.7 %, the same-as-last check 0.7 %).

Built (render/draw.cpp, MeshObject; banjo_renut_mesh_objects, A/B 42): the geometry side as objects. A mesh object is
what a whole-buffer draw's geometry resolves to (each used stream's whole copy, the index buffer's whole copy), kept
under what it is resolved from: the declaration's layout and hash, the vertex shader (object and hash) and its strides,
each used stream's two fetch words, source and stride byte, the draw's base, count and first index, and the index
buffer header's three words. A draw probes one direct-mapped table (4,096 entries of 256 bytes) with those words
(device words already in cache, plus the index buffer's header line) and, on a hit, takes the copies: no stream source
(GetStreamSource reads the buffer's guest header), no keys, no memo slots or entries, no per-stream copy entry until
the bind (prefetched there). Invalidation is pushed: every copy dropped (DropCachedCopy / DropCachedIndices) and every
entry erased (ClearWholeLast, which every erasure goes through) raises g_mesh_gen, and an object of an older generation
answers nothing; the copies' contents are proven once per kick interval as before (the cards or the exact hash,
ProveCopyRange), all of an object's parts at its first use in the interval. With exports pending a hit also passes the
old path's export-window tests for its streams and index buffer (MeshExportFree; b28 skipped the objects whenever exports
were pending, and the emulator's title screen has them pending at nearly every draw: 0 answers). Not used without
banjo_renut_exact_streams. Up to three streams (more take the old path).

Not built: the texture side as objects. FillTextures already keeps each slot's last answer and a memo by texture and
fetch words, with per-page and per-address epochs; a pushed generation would have to be global (resolves bump page
epochs several times a frame), which A/B 35 showed is worse than the fine epochs.


## Record-time translation, the export checks, and what was not built (2026-10-27 afternoon)

The user's request after the Unleashed comparison: make the two game-structure items "more like Unleashed": (1) the
pre-recorded command buffers (each draw handled twice: captured on the draw threads, replayed on the render thread)
and (2) state as GPU register words (decoded on the render thread). Then an ARM pass over both. What the code and the
shadowsort city profile (b24, render thread) showed first, and the user's choice:
- The register words are used nearly as they are: FastRenderState copies ten words, a texture slot's fetch constant is
  six byte-swapped words (DeviceTextureFetch, 0.5 %). There is little decoding to move.
- Two thirds of Replay's 11.5 % in that profile were the look-ahead (5.5 %) and the shadow sort (2.4 %), both off since
  b25. What is left: ApplyState (about 2.6 %: the changed chunks copied into the device image) and the op's fields.
- So (1) + (2) as asked: about 2-3 % of the render thread. The user chose "all three, in that order": the export
  checks first, then (1) and (2) with the ARM pass, then the shadow-caster culling behind its own key.

**Built: record-time extents for the export checks** (banjo_renut_record_extents, A/B 43; _verify in Banjo/Debug).
With memexport results pending at nearly every draw, each draw tests the textures it samples against the pending
export windows. The answer was kept in a 512-entry table by fetch words and the pending exports' generation; the
generation changes several times a frame (each memexport draw, each flush), so the answers rarely survived, and the
table's line missed at each slot (1.8 % of the render thread on the test alone in the profile). Now the recording
threads work out each changed texture slot's guest extent as they record (NewOp: the slots whose fetch chunks the op
changed, TextureGuestExtent's computation through a memo of their own per recording device), the op carries them, Replay
keeps a per-depth table of the device's slots, and the check tests the extent against the 1 KB export bitmap (the
memo's own exact first test) without a table: no answer to go stale, no line to miss. Slots that hold no texture's words
(the last ones share their bytes with vertex fetch constants) and draws outside a replay take the table as before.
This is step (1)'s record-time translation applied where the render thread paid for it.

**Built: SortFacts only with the shadow sort on**: about thirty device words and three hashes a recorded draw on the
draw threads, read only by the sort's planner (off since b25).

**Not built: (2), the replay without the device image.** The image is the replay's host state (the delta model
Unleashed has, in register words): ApplyState copies only each op's changed chunks (a few 64-byte copies a draw). A
view that reads each word through a pointer per chunk instead costs an indirection at every one of the draw's reads,
more than the copies it saves. The op's fields Replay reads already sit in its first two lines (the new extents
offset went into the padding next to 'fields').

**ARM pass over the afternoon's code** (2026-10-27): mesh objects' hits read three lines, not four (the hit's values
on line 2, the guest ranges a new interval's proof reads on line 3), and their key compare is one inline 64-byte compare
(a variable-length memcmp was a C library call); the changed fetch chunks' slots from a 12-entry table (a 26-step loop at
every op); the XMA frame cache's key on the A57's CRC32C instructions (two streams of 8 bytes; a multiply-xor chain
before); the system core's idle sampler tests its 100 ms on the counter before the system call.


**Built: shadow-caster culling** (banjo_renut_shadow_cull_indices, 0 = off; A/B 44; NOT pixel-identical, the user's
call). The census (banjo_renut_diag_shadow_census, emulator title screen): the depth-only passes go to two square depth
targets, 1024x1024 and 768x768 (the shadow maps), about 6,500 draws a second; by index count <=36 about 3 %, <=300 about
14 %, <=3000 about 65 %. Replay skips a draw of at most the key's indices that writes depth only (depth test and write
on, no stencil, no colour write) into a square depth target (>= 256) before any of the draw's work; its state stays
applied and its changes go to the next draw (the hints and the constants' dirty groups keep gathering). At 300 the
emulator skipped about 45,000 draws per 10 s (more than the census's fast-path count: the cull also takes draws the slow
path would have drawn). The title-screen flyover picks its camera segments at random, so the emulator gives no matching
before/after frames: A/B 44 switches the cull every 30 s in one session for the console comparison.

Package: builds/2026-10-27_1455_fourcore (NRO 13c8107ca771, b33): four-core defaults, mesh objects, XMA cache, record-time
extents, the ARM pass, the cull (off; renut_shadowcull.toml / renut_shadowcull_ab.toml).


## Phase A on the console, then replay, copies, geometry and deko3d like Unleashed (2026-10-27 evening, c1-c3)

Phase A (texture objects resolved by the recording threads, package b35): console A/B 45 -2.26 ms (-4.3 %, 4 of 4
pairs), static city 18.6 fps at 50.3 ms (3 cores by mistake). The b35 profile (rex_profile's render-thread PCs, named
with `rt_fn_lines.py`: objdump -dl's line markers, since addr2line gives no lines for the big LTO functions) showed
where the rest goes; the user then asked for the constants, deko3d recording and copies, and "make replay and the
geometry proofs like Unleashed". Built (each exact, own key):

- **Replay stream** (A/B 46). Op records were 288 bytes: every other op started mid-line and a draw read up to four
  lines (three loads were 4 % of the thread). Ops are 64-aligned; a draw's op is its header line and an `OpCall` line
  (the call without pointers, UP data by arena offset, the shaders' recorded words). Prefetch in three stages: the op
  three ahead's lines; the shader memo sets of the op two ahead; the next op's arena state, resource and extent records
  and shader records (`PrefetchShaderRecordLocked`: a look in the memo set fetched an op before). Nothing decoded ahead.
- **Constants**. The push read the shader's used registers and literal count from its cache entry (a miss at shader
  changes): both are in the record's draw summary now. Run lengths by one count; literal runs prefetched.
- **Fetch cache** (A/B 47). Vertex fetch data (bone palettes, instance data) re-copied from guest memory at every kick
  (~600 MB per 10 s on the console, the bulk memcpy) gets the whole-buffer copies' rules: a pool copy once unwritten for
  three kicks, proven once per interval. Emulator: ring copies 240 -> 5-16 MB per 10 s; verify 0 differ.
- **Geometry objects** (A/B 48). Stream sources and index buffers resolved to objects by the recording threads (by the
  words the hardware takes at the draw); the render thread keeps each object's whole copy, two answers for a stream
  (shadow and main pass shaders). Replaces the mesh objects (A/B 42 measured -0.07 ms: keyed per submesh, 340k made per
  10 s). Emulator c2 (one answer): 60 % of draws answered; c3 has the second answer.
- **deko3d recording**. The pipeline object's draw-time fields first (they were before and after a kilobyte of blend
  and vertex state), prefetched once the fast path knows the pipeline; its shader objects prefetched before
  dkCmdBufBindShaders; single-piece uniform pushes inline; `Deko3DShaderReadsVertexId` kept in the draw summary (the
  fast path's hottest line in b35).

Not built: Phase B (loader hooks): see docs/porting-guide-renderer.md 1b. Not changed: the per-interval store-card
proofs (they are what keeps whole copies exact when the game writes a buffer between kicks; five lines a proof).

Verify (emulator title screen, c2): Phase A 27.4 million answers 0 differ (9,037 headers rewritten after recording, 0
bind differently); store cards 0 changed under clean cards; fetch cache 0 differ.
c3 (two answers per stream object): geometry objects answer about 97 % of the whole-buffer draws (60 % with one); Phase A
24.8 million 0 differ; export checks 1.7 million 0 differ. c4 puts the stream's shader stages under their own key
(banjo_renut_replay_stream_shaders, A/B 49).

Emulator four-core test mode (banjo_system_core_test: three cores, the "system core" shared with game threads): c3 froze
at audio start-up 2 of 2 times (the whole process: not even the stall watchdog wrote); c3 boots without the test mode,
and with it when the replay stream is off; c2 boots with it. Read as the emulator's scheduler with the new timing
(Ryujinx warns "too few processor cores"); the console's core 3 holds no game threads. banjo_renut_replay_stream_shaders
is the switch if a console ever shows it.


## Pitfalls found

- API-level stage 1 converted the alpha test to SetShared but left the alpha-to-coverage test reading
  sc.alpha_test_func, the kept block's (the draw before's) value: every other alpha-to-coverage draw lost its alpha
  test (the console's title-screen grass as solid cards). Fixed in builds/2026-10-26_2020_alphafix (NRO a23b4d2c94fe),
  confirmed on the console by the user. Any read of the persistent block before the draw sets that
  field is the previous draw's value.
- The emulator's gl_VertexID includes a draw's base vertex; whether the console's does is not known (no deko3d source
  here to check), so whole-buffer draws do not depend on it: they bind their copies from the base vertex and draw with
  none (Geometry::whole_base), as the range copies always did. (It was first suspected for the grass; the alpha test
  above was the cause.)
- An always_inline helper that builds a large temporary puts it in every caller's stack frame: SyncBound's
  `g_bound = Bound{}` (about 25 KB, once per command list) made FastDrawLocked's frame 31 KB and Clear/ResolveStateLocked's
  29 KB in b12 (3.4 and 1.2 KB before); the reset is ResetBound now, cold and out of line. tools/switch_frame_sizes.py
  old.elf new.elf lists the renderer and plume functions whose frames grew (2026-10-27).
- Editing a header while a build compiles can mix two layouts of a struct in one binary (b13 was safe: its one compile
  had finished 27 s before plume_deko3d.h changed). Edit sources only between builds. On Windows, stopping the
  background shell of a build leaves cmake / ninja / ld running, and their later children fail to start (0xc0000142):
  the link fails and the old NRO stays. Let a build finish; check the NRO's time before copying it.
- banjo_renut_ab_targets kept only targets below 32 (AbTargets' parse, and Stats::by_target[32]): the lists of the
  night packages (39 ... 26) rotated over 31-26 only; 32-39 were dropped without a word. Raised to 64 (2026-10-27,
  found when A/B 40 gave no line). The single target (banjo_renut_ab_target) had .range(0, 19): every target from 20
  on was refused, so with those NROs 32-39 could not be measured at all. Now 0-63.
- A patch script that replaces an expression everywhere in a function also replaces it in the declaration it just
  added (`const int ab = ab;`, b9-b11). GCC compiles that silently without optimisation-time warnings enabled; grep for
  `T x = x;` before building (2026-10-27).
