# Renut CPU work ported from sa2-nx (2026-10-20, package 36o)

sa2-nx (Sonic Adventure 2's port, `D:\sonic adventure 2\sa2-nx`) took Renut's renderer from banjo-nx and made its CPU
paths cheaper: `docs/renut-cpu-work-for-banjo.md` in its tree lists 19 items. On SA2's console (City Escape, 1,400
draws a frame) its render thread went from 17.6 to 11.1 ms a frame and its game thread from 19.2 to 15.5 ms. This is
what banjo-nx took, what it changed for N&B, and what it left out. Every item is a key (`banjo_renut_*`, on by
default); `configs/banjo_renut_sa2_cpu_off.toml` switches them all off for the whole A/B.

## N&B is not SA2: where the draws come from

SA2 records no command buffers, so all its draws were immediate (a whole device copy queued per draw). N&B records
most of its draws: the 10 s line `[renut] draw sources` (new) counts them. Emulator run 118, scripted route:

| Part of the route | Immediate draws / 10 s | Replayed from command buffers / 10 s |
|---|---|---|
| Intro, title, menus | all (49-24,417) | 0 |
| Gameplay | 6,000-19,000 | 21,000-110,000 |

So in play about a fifth to a quarter of the draws are immediate. Items 1-3 (device deltas) and sa2-nx's chain reuse
only reach those. banjo-nx extends the chain reuse to the replayed draws (below), which already kept deltas
(`guest/command_buffers.cpp`: `Op::plain_dirty`, `vs_dirty`, `ps_dirty`, the same scheme sa2-nx's deltas copy).

## Taken

| sa2-nx item | Key | Files |
|---|---|---|
| 1 device deltas for immediate draws | `banjo_renut_snapshot_deltas` (+ `_verify`) | backend/queued.cpp/.h, d3d/hooks/device.cpp (Swap: `SnapshotDeltasNewFrame`), guest/command_buffers.cpp (`NewOp`: `ForgetSnapshotDeltas`) |
| 2 constants by the chain | `banjo_renut_snapshot_deltas_constants` | backend/queued.cpp |
| 3 narrow compare | `banjo_renut_snapshot_deltas_narrow` | backend/queued.cpp (N&B's ranges, below) |
| 4 NEON line compare | `banjo_renut_neon_compare` | gpu/line_compare.h (sa2-nx's sa2_line_compare.h) |
| 5 capture pool in batches | `banjo_renut_capture_batches` | backend/queued.cpp |
| 6 hooks' fast paths | `banjo_renut_callbacks_fast` | guest/callbacks.cpp/.h, backend/queued.cpp, guest/command_buffers.cpp |
| 7 render thread woken in batches | `banjo_renut_wake_batch` (16) | backend/render_thread.cpp |
| 8 next item prefetched | `banjo_renut_prefetch_next` | backend/render_thread.cpp/.h (`QueuePrefetched`) |
| 9 done signal only with a waiter | `banjo_renut_done_notify_waiters` | backend/render_thread.cpp |
| 10 one clock read per batch | `banjo_renut_batch_clock` | backend/render_thread.cpp |
| 11 chain reuse (key, targets, viewport, textures, pipeline, framebuffer) | `banjo_renut_chain_reuse` (+ `_verify`) | render/draw.cpp/.h, render/pipeline.cpp/.h, render/surfaces.cpp/.h |
| 12 PushFloatConstants fast path | (with the deltas' constants) | render/draw.cpp |
| 13 shared constants: inline/NEON lines, fetch slots skip, partial clear, fetch data unread | `banjo_renut_inline_line_compare`, `_fetch_slots_skip`, `_shared_partial_clear`, `_fetch_data_unread` | render/draw.cpp |
| 14 stream mask, texture defaults copy, lean diagnostics | `banjo_renut_stream_mask`, `_texture_defaults_copy`, `_lean_diagnostics` | render/draw.cpp, render/vertex_format.cpp/.h |
| 16 core layout (OFF by default since 36q, below) | `banjo_renut_core_layout` | SDK system/xthread.cpp (`RexSetGuestThreadCoreLayout`, threads register themselves), core/threading_switch.cpp/.h, d3d/hooks/device.cpp |
| 19 no Vulkan driver | (package 36m, banjo-nx's own) | sdk/cmake/rexglue_switch.cmake, sdk/src/ui/switch_vulkan_stubs.c |

`render_thread.cpp`'s SA2 read hook (`rex::input::SetReadHook`) was left out: banjo-nx's input system already calls the
late-input delay and the probe at the controller reads, and a second call would delay the read twice.

## What was changed for N&B

- **The narrow compare's unread range.** SA2 skips device bytes 11008-12223 (its private block up to its vertex
  declaration). N&B's private block lies 128 bytes earlier (d3d.h: the creation thread at 10888, the fence tags at
  10908 and 10928, the swap flags at 10941) and its vertex declaration is at 11820 (SA2 12240). The render thread's
  last packet read is `m_MiscPacket` (up to 0x2A50, d3d_native_state.cpp); nothing it reads lies between 0x2A50 and the
  vertex declaration. So N&B skips 10880-11775: 39 of its 71 chunks compared (SA2 40 of 77; N&B's snapshot is 12,712
  bytes, SA2's 13,088).
- **The reuse checks' device offsets.** sa2-nx's `ChainChanged(chain, 12816, 12835)` (render targets) and
  `(12920, 13023)` (texture words) were SA2's offsets; banjo-nx uses `d3d::kDeviceOffsetRenderTargets` ..
  `kDeviceOffsetDepthStencil + 3` (12440-12459) and `kDeviceOffsetTextures` .. `+ 4 * kTextureFetchSlots - 1`
  (12544-12647). The other ranges (fetch constants 1152-1775, packets 10112-10599, the word at 0x29C0, the
  predication) are in the public part of the device, the same in both games.
- **What the reuse may skip.** Checked against banjo-nx's own versions: `MakeKey` reads the bool constants and the
  control/values packets (10112-10599), the shaders' entries and bool masks, the targets, the primitive, the
  instancing, the alpha test and fixed settings plus the in-run A/B half (`g_ab_off`, compared); `ComputeViewport` the
  window/values/control packets, 0x29C0, the predication, the targets and a fixed setting; `GatherTargets` the target
  words and their descriptors. All within sa2-nx's conditions.
- banjo-nx's skin once (`BindHashed`, `g_bound_streams_hash`, `FurSkinLocked`, `fetch_window`) kept as it was beside
  sa2-nx's hunks (the stream mask walks the same streams in the same order, so the stream hash is the same).

## banjo-nx's extension: chain reuse for replayed draws (`banjo_renut_chain_reuse_replays`)

A recorded command buffer's ops already say which 64-byte device chunks changed (`plain_dirty`: the fetch constants'
12, then the tail's from the bool constants). `Replay` now ORs them up between draws and gives each replayed draw a
`ChainHint` with those chunks (the fetch constants' 12 are the chain's chunks 18-29, the tail's the chain's from 30).
Both producers take their numbers from one counter (`NextChainSeq`, draw.h): a draw's hint is consecutive only if no
other draw (a nested buffer's, the immediate chain's) came in between; otherwise it marks every chunk changed, so no
memo of another device's draw is taken. `banjo_renut_chain_reuse_verify` checks the replayed draws too.

## From futari-nx: Begin*Vertices draws in the chain (`banjo_renut_deferred_chain`, OFF by default since 36q)

**Off by default since package 36q.** The chain is one for every device, and N&B creates two D3D devices. A
Begin*Vertices delta is made at Begin and queued at its device's next hook; a draw on the other device made and
queued in between rebases the chain to that device, and the render thread then applies the delta on top of the wrong
device (wrong targets, shaders, vertex data). It needs no second thread: Begin on device 1, a draw on device 2, then
device 1's next hook. In the emulator 4 of 8 runs with it on hung in loading or the first frames, 0 of 3 with
sa2-nx's chain alone (whose deltas are made and queued in one step under the chain's lock). futari-nx told. The text
below is how it was meant to work.


sa2-nx kept a Begin*Vertices draw (made at Begin, queued at the next hook once its vertices are written) a whole device
copy outside the chain, since it is queued later than it is made. futari-nx (88 % of its draws are Begin*Vertices) put
it in the chain: `QueueDeferredDraw` makes it with `CaptureDelta` under the chain's lock, and `FlushDeferredVertices`
queues it under the chain's lock too (taken before the deferred map's). Every hook flushes its device's deferred draw
before making any other capture, so the queue still holds the chain's draws in the order they were made. In N&B a
recorded op made meanwhile (`NewOp`: `ForgetSnapshotDeltas`) only makes the next capture a whole one, queued after the
deferred draw. A draw without scratch (never queued) stays a whole capture. N&B has about 1,500 of them per 10 s in
play (run 120), so this is small here; it was the user's request through futari-nx.

## The core layout is off by default (package 36q)

`banjo_renut_core_layout` hung the first Swap in 4 of 12 Ryujinx runs (emulator runs 123, 125, 126, 129: the guest froze
while the game was still loading on other threads; the line "[renut] banjo_renut_core_layout: ..." never came, so
`RexSetGuestThreadCoreLayout` never returned). It re-masks every running guest thread (`svcSetThreadCoreMask` on their
handles, `set_affinity_mask`) while it holds the kernel object table's lock (`GetObjectsByType`). The failures came with
a busy host, the passing runs with an idle one. Confirmed apart from the deferred chain (below, the other hang):
emulator run 129, the deferred chain off and the core layout on, hung the first Swap the same way; with both off, 0 hangs in
the runs since. It may be the emulator alone (sa2-nx has it on in two console runs), but it is not worth a hang on the
console: off by default, `configs/banjo_renut_core_layout_on.toml` to try it. sa2-nx told.

## Left out

- **18, the store cards' small-range loop** (`MarkStoreCardsAt` in sdk/include/rex/system/xmemory.h): SA2's natives
  called it about 20,000 times a frame. banjo-nx's console profile (package 36m): `memset` 0.3 % of the drawing
  thread, 1.0 % of one other. Changing the header rebuilds all generated code; not worth it now.
- SA2's depth-smaller-than-colour warning (a diagnostic), its guest-declaration lookup (`FindGuestDeclarationLocked`,
  SA2's Direct3D) and its retargeting.

## Verified (emulator, 2026-10-20)

- Run 120, both verify keys on (`configs/banjo_renut_cpu_verify.toml`), 7 minutes of the scripted route into play:
  every delta-rebuilt device compared byte for byte with a whole copy, 0 differences (6,300-7,100 immediate draws per
  10 s checked; 4.5-5.8 KB kept a draw instead of 12.7 KB, about 4.5 chunks and 7-9 constant groups a delta). The reuse
  verify (key, targets, viewport, texture words, pipeline, framebuffer, for the immediate and the replayed draws): one
  difference, the pipeline, from a race sa2-nx's code has too (fixed: the prebuild epoch is read before the lookup;
  sa2-nx told). In that run's late play: about 8,000 immediate and 320,000-355,000 replayed draws per 10 s.
- Run 121, verify off: the same pictures as run 116 (before the port) through the intro, the title, the pizza scene and
  Spiral Mountain; no errors. Its frame rate is not a measure: the PC was saturated (other ports' LTO builds and a
  Defender scan, 98-99 % CPU with no emulator running), and the emulator's speed follows the host's.
- Not yet measured on the console: the render and game thread times against `configs/banjo_renut_sa2_cpu_off.toml`.
