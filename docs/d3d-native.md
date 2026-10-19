# Direct3D as native code: the draw path (2026-10-14)

The order: replace the game's Direct3D draw path with native code, the way sa2-nx did (its `docs/api-renderer.md` is
the method and names Banjo), and do it ARM-first, because sa2-nx's first native layer was correct and slower on the
console than what it replaced. This file is Banjo's record of it: the census, the device layout of this XDK build, what
was written, how it is checked, and what it measures. Sources: sa2-nx `app/src/me/sa2_d3d_api.cpp`, `sa2_api_draw.cpp`,
`sa2_api_internal.h`, `tools/d3d/` (GPL-3.0, same project family).

**In one paragraph.** `D3DDevice_DrawIndexedVertices` and `D3DDevice_DrawVertices` run as native code
(`app/src/me/banjo_d3d_native.h`, keys `banjo_d3d_native_draw_indexed` / `banjo_d3d_native_draw_vertices`): the
draw-time state flush both functions inline (which dirty masks, in which order, with which registers), the three
writers of register runs, and the draw packets with and without the predicated-tiling wrapper. They write **the same
PM4, byte for byte, into Direct3D's own command buffer** and leave the device structure as Direct3D leaves it. The
draw-time shader load and two rare blocks are still Direct3D's own recompiled functions, called at the same place
with the same registers. It is a game-thread saving only: the ring thread and the GPU, which set the frame rate, see
identical input. Both keys are **on by default**: verified (357,931 + 6,573 calls compared in the
final verify run, 0 differences; 0 in every normal run) and measured not slower (PC in-run A/B: 0.68 against 0.84 us
per DrawIndexedVertices call, -19 %; A57 estimate about -40 % of the code it replaces, about -20 % of the whole call).
What it buys is small and on the game threads (in the level about 0.4 million A57 instructions a frame on the two
recording threads); the ring thread is unchanged (in-run A/B 13.79 against 13.91 ms of work per Swap). Not done: the
draw-time shader load as native code (the biggest part of every draw call that is left), BeginVertices /
DrawVerticesUP, Clear, Resolve and the other PM4 writers.

## 1. The census (`banjo_diag_d3d_census`)

`app/src/me/banjo_d3d_census.h` is sa2-nx's census ported: per Direct3D function, device object and thread, the calls
and the PM4 words and packets each outermost call writes (the device's write pointer +48 before and after). Off by
default; with the key off the renderer's own hooks read one byte.

**Banjo's Direct3D is not one address range.** The game was linked with profile-guided function order: the hot
Direct3D functions sit between hot game functions at 0x8220-0x8229, the rest at 0x8264D-0x82663. sa2-nx hooks a range;
here `tools/d3d/d3d_writers.py` scans the generated code for the functions that call one of Direct3D's space functions
or write after the device's write pointer (73 of 39,674 functions), and `tools/d3d/d3d_census_build.py` writes hooks
for those plus the named entry points of `app/config/renut_gpu_funcs.toml` that have no hook yet (82 hooks) into
`app/src/me/census/banjo_d3d_census_hooks.inc`, re-runs `tools/post_codegen.py` and touches `banjo_me_hooks.cpp`,
which includes the file when it exists. `--restore` removes the file again. The hooks file lives inside `app/src` on
purpose (every post-codegen tool reads addresses named there as hooked) and is never part of a normal build. Each way
about 85 generated files are recompiled (6 minutes on the PC). `tools/d3d/d3d_census_report.py <log>` reduces a log.

Not hooked: the two named memexport helpers (`rex_DoBeginExport...`, `rex_DoEndExport...`); what they write is counted
under the hooked function above them.

### What the game calls (PC, 340 s through title, menus, intro and the first level; per Swap)

The four windows are seconds of the run (the route of the usual test: A every 6 s from second 50; that run shared the
PC with a compile, so the scene boundaries are approximate). "calls" are outermost calls; PM4 words are what those
calls wrote.

| Function | 0-50 s (logos, title) | 50-110 s (title, menus) | 110-260 s (intro) | 260-330 s (level) |
|---|---|---|---|---|
| **all counted PM4 words per Swap** | 67,208 | 95,644 | 134,128 | 147,264 |
| D3DDevice_DrawIndexedVertices `826567C8` | 493 calls / 52,909 words | 682 / 68,347 | 1,089 / 111,129 | **1,161 / 127,330** |
| D3DDevice_DrawVerticesUP `8222E520` (calls BeginVertices) | 41 / 3,218 | 101 / 9,497 | 39 / 5,654 | 8 / 3,284 |
| D3DDevice_BeginVertices `8222CC50` (direct calls) | 12 / 1,724 | 19 / 3,564 | 17 / 3,339 | 17 / 3,264 |
| D3DDevice_DrawVertices `8222C7A0` | 29 / 3,941 | 19 / 2,652 | 16 / 2,544 | 14 / 2,411 |
| FlushState, standalone `82222888` | 7 / 2,559 | 16 / 4,646 | 17 / 4,464 | 17 / 4,469 |
| D3DDevice_Resolve `8222D6C8` | 8 / 808 | 23 / 2,426 | 23 / 2,369 | 21 / 2,160 |
| D3DDevice_Clear `822812C8` | 1.2 / 290 | 5.4 / 1,160 | 5.6 / 1,248 | 5.0 / 1,196 |
| window scissor / tile state writer `82223270` | 30 / 267 | 84 / 683 | 86 / 710 | 83 / 700 |
| D3DDevice_SetShaderGPRAllocation `82220B20` | 10 / 198 | 23 / 444 | 24 / 472 | 24 / 472 |
| `8227A9F0`, `8227B218`, `82278C08` (tiling begin/end, once a frame each) | 1.8 / 556 | 3 / 1,012 | 3 / 1,014 | 3 / 1,013 |
| `822693B0` + `82269618` (a clear-like pair) | 3.6 + 3.6 / 472 | 3.8 + 3.8 / 497 | 3.4 + 3.4 / 450 | 3.3 + 3.3 / 439 |
| `82657CB0`, `8222AA18`, `82230838` and the rest | under 300 words | under 450 | under 550 | under 550 |

So **DrawIndexedVertices writes 79 to 86 % of all PM4** (110 words a call in the level), and nothing else is above
3.5 %. Nested under it per Swap in the level: the draw-time shader load 1,152 times (on 95 to 100 % of the draws),
the literal-constants helper 1,891, the depth/0x2203 update 788, the re-send block 31.

**Devices and threads.** Five device objects appear in the run, never more than three at a time: the main device
(`40E6BA00`, the main thread, 164 calls a Swap in the level) and two recording devices at a time (`452A9F00` /
`452AFE00` in the title and menus, `41AC4F80` / `4536A000` from the intro on), each used by one worker thread
(806 and 488 calls a Swap in the level). **Every DrawIndexedVertices call of the level is on a recording device**;
the main device draws with DrawVertices, DrawVerticesUP and BeginVertices and does the clears, resolves and tiling.

**Draw-time facts** (the tests of the inlined FlushState, per DrawIndexedVertices call on the recording devices in
the level): float constants dirty on 52 % (vertex shader, 1.3 blocks of four vectors a call) and 48 to 99 % (pixel
shader, 1.5 blocks); **the shader load on 95 to 100 %**; fetch constants on 98 to 100 % (3.8 dirty slots a call);
register group 0x2200 on 4 to 25 %, 0x2100 on 4 to 19 %, 0x4900 on 3 to 6 %, every other block under 1.5 %; more than
65535 vertices on 0.2 to 0.3 %; the predicated-tiling wrapper (flag bit 0 of +10940) on 38 to 49 %. Flag bytes
+10940..+10944 at a draw: recording devices `80 00 20 00 00` and `B1 00 20 00 00` (rarely `80 04 ..`, `B1 04 ..`,
`B0 ..`); main device `00/20/21 00/04 24/04/14 00 00`.

## 2. The device structure of this XDK build (24,064 bytes)

From the listings (`python tools/d3d/ppcfn.py <address>` prints a function's PowerPC from the generated code's
comments). The shadows and the command buffer words are where sa2-nx's 2012 XDK has them; the flags are 128 bytes lower
(futari-nx's offset), the bound objects 376 lower.

| Offset | What |
|---|---|
| +0, +8 | dirty float-constant blocks, vertex / pixel shader (64 bits, bit 63 first, one bit = four vectors) |
| +16 | dirty register groups: bits 0-11 group 0x2200, 12-16 0x2180, 21-41 0x2100, 42-57 0x2000; bits 17-20 ask for the shader load |
| +24 | dirty fetch constants (low 32 bits, six registers a bit from 0x4800) and group 0x2280 (bits 34-54) |
| +32 | dirty misc: group 0x2300 (low 38 bits), 0x2380 (bits 38-45), the 0x2388 block (bits 49-54), 0x4900 (bit 56) |
| +40 | the groups the re-send block handles |
| +48, +52, +56 | command buffer: the last word written, the segment's limit, the soft limit |
| +1152 | fetch constants' shadow |
| +1920, +6016 | float constants' shadows (registers 0x4000, 0x4400) |
| +10112 | bool / loop constants' shadow (0x4900, 40 words) |
| +10368, +10444, +10528, +10548, +10596, +10680, +10832 | shadows of groups 0x2000, 0x2100, 0x2180, 0x2200, 0x2280, 0x2300, 0x2380 (so 0x2001 = +10372, 0x2104 = +10460, 0x2180-0x2182 = +10528..+10536, 0x2203 = +10560, 0x2208 = +10580) |
| +10896 | pointer to the GPU's last completed fence; +10908 the fence of the next submission |
| +10940..+10944 | flag bytes. +10940: 0x01 draws get the predicated-tiling wrapper, 0x10 / 0x20 / 0x40 tiling state of the re-send block and the shader load, 0x80 a recording device (set by CreateDevice for device type 2) |
| +11820 | vertex declaration; +11832 the strides the loaded vertex shader binding was patched for (16 bytes) |
| +11852 | surface flags |
| +12122, +12187 | viz table and its index (the draw packet's first word) |
| +12436 | index buffer; +12440..+12456 render targets and depth |
| +12528 | stream strides (16 bytes) |
| +12692, +12696 | bound pixel / vertex shader |
| +12700 | the 0x2208 value last sent under tiling; +12708, +12712 saved tile mask / select |
| +12728..+12748 | the targets at BeginTiling and the tile count; +13176 base of register 0x2000 |
| +13248 | the tile-extents list: first block, current block, next entry (+13256), end (+13260), device |
| +13504 | debug object |
| +14972 | a critical section (lock count +14988, owner +14996): the inline-data allocator's |
| +16544..+16719, +21676, +23376 | the vertical-blank interrupt handler's counters, flip queue, time stamps |

Functions (this XDK build against sa2-nx's): the draw-time shader load `8222BF10` (sa2 `82556A60`), its helpers
`8222A798` (depth / 0x2203), `8222BDC8` (literal constants; with an inline-copy form for recording devices),
`822127F8` (is the binding patched for this declaration), `82656EE0` (inline patched copy), the fetch patcher
`821E5218`; the re-send block `8226AAB8` (sa2 `825555A0`, here with the per-tile part); the 0x2388 block `8220E2B0`;
the writers `8222D0E8` (constant blocks), `8222CB90` (register words), `8222AB88` (fetch constants); the space
functions `82210AB0` (kick / next segment, soft limit), `821E2940` ("more space" of the writers, hard limit),
`821F7C18` (n words), `8222D240` (inline data), `8220F800` (a new tile-extents block). The standalone FlushState is
`82222888`. The FlushState order differs from sa2-nx's where futari-nx's does: misc is 0x2300, 0x2380, the 0x2388
block, 0x4900, and there is no misc-bits-62-63 block.

## 3. Same PM4 or records: the choice

sa2-nx's native layer writes host records for its ring thread (named by marker NOPs) instead of PM4. Here the native
code writes **the same PM4**. Why:

1. **Banjo's command buffers are not a stream.** The engine records its draws on two worker threads into command
   buffers of their own (recording devices), the main device runs them once per EDRAM tile, and Direct3D patches a
   predicated draw's tile mask in the command buffer after the draw call (the placeholder `0xFFFFFFFF`), from extents
   the GPU writes back. Identical packets keep every one of these mechanisms as it is. A record transport has to keep
   records per stream, replay them per tile and per re-run, and apply Direct3D's later patches to them: that is what
   `banjo_direct_state` (step 1, in the tree, off by default) already does for records decoded from the PM4.
2. **The ring side of records is not faster here.** The ring's part of a record transport is the same whether the
   record is decoded from PM4 (step 1) or written by native code. Measured on the PC in one run (in-run A/B, 2 s
   halves, `banjo_direct_state`, 340 s to the level, this tree on 2026-10-14): records ON 25.33 ms of ring work per
   Swap (11.76 us per draw, of it 2.19 us "PM4, records, identification"), OFF 24.35 ms (11.25 us, 1.93 us): the
   records cost 4 % more ring time, as on 2026-10-07 (15.5 / 16.5 ms then). The PM4 parser is vectorised already; a record saves the byte swap and pays for its
   index, its release and its checks. So records would not buy ring time, which is the only reason to take their
   risk. (The console A/B of step 1, `configs/banjo_direct_ab.toml`, is still the open measurement; if it wins there,
   the native writers below can feed records with one more sink, see "Next".)
3. **Identical packets can be checked exactly** (a memcmp), with no canonical forms, and nothing downstream changes:
   not the ring, not `banjo_gpu_tiles_once`, not `banjo_direct_state`.
4. **Any part can stay Direct3D's own code.** Because the output goes to the same command buffer, the native function
   calls the recompiled helper for what is not written natively yet, at the same place with the same registers. sa2-nx
   had to transliterate the whole shader load before its first native draw could run.

What it costs: the gain is on the game threads only.

## 4. What is native, and what runs the original

`banjo::d3d::native::Draw<indexed>` (`app/src/me/banjo_d3d_native.h`; header-only and included by
`banjo_me_hooks.cpp`, because a new source file means a CMake re-run, which rebuilds the code generator and with it
every generated file):

- **FlushState** as both draw functions inline it: the five dirty masks loaded first (later blocks see those values
  even if a helper changes the device), then float constants (vertex, pixel), the shader load, the re-send block, the
  groups 0x2200 / 0x2180 / 0x2000 / 0x2100, the fetch constants, 0x2280, 0x2300, 0x2380, the 0x2388 block, 0x4900,
  each mask zeroed where the original zeroes it. Masks are rotates (`Rldicr`, `Rlwinm` helpers that mirror the
  PowerPC instructions).
- **The three writers** as one inlined loop each (`ConstantRuns`, `RegisterRuns`, `FetchRuns`): one type-0 packet per
  run of dirty bits, values copied guest order to guest order (no byte swap), the constants' type-2 padding to a
  16-byte boundary, the fetch constants' 8-byte alignment and their end packet (register 0x5000 x 3 = 0, 0x25000, 0).
- **The draw packets**: register 0x2102, the viz word, the DRAW_INDX packet (16- and 32-bit indices), and under
  predicated tiling the tile-mask placeholder, the extents entry and the two event packets, in the original's order of
  loads and stores.
- **Direct3D's own code, called in place**: the shader load `8222BF10` (through its hook: the renderer notes the
  shader objects there), the re-send block `8226AAB8`, the 0x2388 block `8220E2B0`. They write into the same command
  buffer; the native code reads the write pointer and the limit again afterwards.
- **One block left to Direct3D's own writer** when its runs might not fit the segment (the writer then calls for
  space): the test is a sufficient one from the number of dirty bits, so no run is tested twice.
- **The original runs the whole call** (checked before anything is changed) for: more than 65535 vertices (the chunk
  loop: 0.2 to 0.3 % of the calls), a command buffer window that is not plainly usable (not seen), no index buffer
  (not seen), a device object outside the game heap (a precaution for the check's 24,064-byte copy; never seen: the five
  devices of the census are all in the game heap, and the crash first blamed on such an object was the register bug
  of section 5).
- **The original finishes the call** when the draw packets need space (the write pointer is past the soft limit) or
  the tile-extents list needs a new block. At that point every dirty mask is zero, so the original goes straight to
  its draw part: the same instructions from there on.

Registers: the originals leave values in r3-r12; no caller reads one after the call (sa2-nx's
`tools/call_site_liveness.py` over the 24 direct call sites of the two functions). The native code writes r3-r6 only
where it calls a recompiled helper, a subset of what the original writes, and puts r3-r7 back to the call's arguments
before it hands a call to the original. The original's stack frame is linked the
same way (the back chain word at r1-208 / r1-160) and its helpers get the same stack pointer; the frame's other
stores are not made (nothing reads below the stack pointer after the call returns; the check does not compare the
dead frame).

Guest addresses named in the new sources: `8222BF10`, `8222CB90`, `8222D0E8`, `8222AB88`, `826567C8`, `8222C7A0`
(hooked already), `8226AAB8`, `8220E2B0` (called directly: `tools/post_codegen.py` must run once so their callers stop
calling them as inlinable), and the data address `82FB0130` (Direct3D's counter of declaration ids).

## 5. The check

For the first `banjo_hot_guard_calls` (64) calls of each thread and each entry point, one call in
`banjo_hot_guard_period` (4096) after that, and every call with `banjo_cpu_verify` (`Checked`):

1. the device (24,064 bytes) is copied into a per-thread area of guest memory; the copy's command buffer is a scratch
   area with the real write pointer's alignment phase (the constants' padding depends on it) and far limits;
2. the guest memory a draw may write outside the device is captured: the bound vertex shader's binding states and
   program descriptors, its bindings' microcode (the shader load patches it), the declaration's id and Direct3D's id
   counter, the tile-extents entry;
3. **the native code runs on the copy**; the copy, its packets and what it wrote outside the device are kept, and the
   outside memory is put back;
4. **the original runs on the copy**, from the same bytes and into the same scratch area;
5. compared byte for byte: the packets, the whole device structure (no field left out), and the tile-extents entry
   (the one guest location outside the device the native code writes itself);
6. the outside memory is put back again and **the original runs for real**: a checked call always gets Direct3D's own
   result.

Not compared, counted: a call the native run declines (its reasons are in the report line), and a difference that
comes with the GPU's completed fence moving between the two runs (the binding check reads it). The shader-state
regions are captured and put back but not compared: only Direct3D's own shader load writes them (both runs call it),
and other threads load the same shader objects and bump the same id counter between the two runs.

**Why two copies.** sa2-nx runs the original on a copy and the native code for real, and compares the copy with the
real device. The first version of this check did the same the other way round (native on the copy, original for real)
and found, in that order: the vertical-blank interrupt handler's counters and time stamps (+16544..+16719, +21676,
+23376), the critical section of the inline-data allocator (+14972..+14999, which the recording threads take on the
main device) and a block the GPU and the interrupt callbacks write (+10944..+11180 at least) all change in the real
device while a check runs: four false differences in about 700,000 comparisons, each of which switched the entry point
off. A private copy has none of that, so nothing needs to be left out, and the check needs no list of what other
threads write.

A difference logs `[d3d native] DIFFERENCE ...`, counts into the verify line's `differences:`
(`me::native::VerifyDifference`) and switches that entry point off for the run. One checked call in eight makes the
native run send every block through Direct3D's own writers ("forced helper form"), which is how the registers passed
in the "does not fit" branch are compared. Every 10 s: `[d3d native] <entry point> ON: ...`.

In-run A/B: `banjo_ab_target = 19` (16 is taken by the main tree's deferred recording) with
`banjo_direct_state_ab_s`; in the "off" halves one original call in 16 is timed like the native ones, so the report
has both costs per call from the same run.

**A bug the checks did not catch, found by crashes.** When the native code hands the rest of a call to the original
(late: the draw packets need space, or a new tile-extents block), the original must get its own argument registers;
the first version left the registers a helper had written, and the original then drew with garbage arguments. Verify
mode cannot show this (a declined copy run is not compared, and the real call is the original's); normal PC runs
crashed about 30 s in (two threads at once). The arguments are now restored before every late hand-over.

## 6. ARM first

sa2-nx's checklist (its `docs/api-renderer.md`, section 6) against this code:

| sa2-nx's cause | here |
|---|---|
| a `uint32_t` count next to the buffer it indexes | pointer cursors and locals only; nothing is written through a struct |
| byte swaps word by word | none: every copy is guest order to guest order (the packets are guest order) |
| `thread_local` on the usual path | none. The per-thread countdown to the next checked call and the timing tick are in the guest context (`PPCContext::host_counters[60..62]`); the check's scratch is a `thread_local` pointer touched in checked calls only |
| the whole guest context copied per helper call | never: helpers are called on the caller's own context, as the recompiled code does. One copy per checked call |
| needless copies | one copy per run, shadow to command buffer |
| a ring that does not fit the L2 | no new buffer: the packets go where Direct3D puts them |
| shared counters | none written on the usual path (the statistics are bumped on declines, checks and the 1-in-16 timed calls) |

**What the Switch compiler makes of it** (`tools/d3d/d3d_arm_lines.py`: this tree's sources compiled with the flags of
the main tree's Switch `build.ninja`, read only, plus `-g1`; per source line, and per block of the recompiled
originals). Static sizes: the native `Draw<true>` is 2,394 A57 instructions (eight inlined register-word writers, two
constant writers, the fetch writer, both draw forms, the fallbacks), the recompiled `sub_826567C8` 640 plus the
functions it calls.

One thing it caught before any run: GCC vectorised the register-word copy loop (a run of one to three words got about
45 instructions of vector set-up and tail handling). That loop is now an overlapping 4/8/16-byte copy without a loop
(`CopyWordsSmall`): about 40 instructions per run instead of about 85.

**Instructions per call on the usual path** (read along the block graph of the A57 code; an estimate, not a console
measurement; the draw-time shader load is Direct3D's own code on both sides and is left out of both columns):

| Part of a DrawIndexedVertices call (recording device, the census's usual case) | original | native |
|---|---|---|
| entry, mask loads, tests, the shader-load call and its re-reads | about 150 (the recompiled function's blocks) | about 125 |
| vertex shader constants, one run of one block | 159 (hook + hand-written native + call set-up) | about 97 |
| pixel shader constants, one run of one block | 159 | about 85 |
| fetch constants, two runs, four slots | about 252 (the localized copy's average + its hook) | about 120 |
| the draw packets (non-tiling) | about 145 | about 75 |
| **total without the shader load** | **about 865** | **about 500** |

So about 365 A57 instructions less per call, about 40 % of what the native code replaces, and about 20 % of the whole
call once the shader load (about 900 instructions, unchanged) is counted. In the level that is about 1,160 calls a
Swap on the two recording threads together: about 0.4 million instructions a frame, well under a millisecond of the
two recording threads together at 1020 MHz. The PC's in-run A/B (below) gives the same share: 19 % per call.

## 7. Results

All PC, through `tools/run_pc_test.sh` (A every 6 s from second 50, 340 s: title, menus, intro, the first level);
logs in `app/out/pc-me`.

| Run | Log | Result |
|---|---|---|
| Verify (`banjo_cpu_verify`), final code | `d3d_final_verify.log` | DrawIndexedVertices: 358,328 checked, **357,931 compared, 0 differences** (44,764 in the forced helper form), 408 not compared (404 with more than 65535 vertices, 4 needing a new extents block), 0 raced. DrawVertices: **6,573 compared, 0 differences** (805 forced). `[verify] ... differences: 0`, no DIFFERENCE or MISMATCH line |
| Verify, the check's first form (native on the copy, original for real) | `d3d_v1_verify.log` (overwritten) | 125,620 and 5,075 compared; the false differences described in section 5 |
| Verify, two copies, before the copy fix | (overwritten) | 549,951 and 15,328 compared, 0 differences |
| Defaults | `d3d_normal.log` | 8.34 million native DrawIndexedVertices calls, 154,000 DrawVertices; 2,233 + 167 compared, 0 differences; the original finished 28,367 calls (draw packets needed space, 0.34 %) and ran 23,096 (more than 65535 vertices, 0.28 %); 479 new extents blocks. Self-timer: 0.92 us per native call (with the shader load inside), 320 ms per 10 s on the drawing threads together = 3.2 % of one core |
| In-run A/B, target 19, 2 s halves | `d3d_ab17.log` (target 17 in the copy it was built in) | ring thread ON 13.79 / OFF 13.91 ms of work per Swap (6.21 / 6.36 us per draw, about 2,200 draws a Swap): no change, as identical packets must give. Cost per call, same run: DrawIndexedVertices native 0.681 us (286,666 timed), the original 0.840 us (287,472 timed): **-19 %**; DrawVertices 0.583 against 0.627 us |
| Picture, same run | `capturas/latest.mp4`, `tools/ab_flicker.py --period 2` | the largest phase is the known one: 24.3 every 6 s (5.30, 11.30, 17.30, 23.30 s; the washed-out frames after each A press, present with every enhancement off), 0.2 to 0.9 at every other flip of that phase. No difference that follows the switch. (The packets are byte-identical, so none is possible; this is the required look.) |
| In-run A/B of `banjo_direct_state` | `d3d_direct_ab.log` | see section 3 |

The PC is shared with other projects: compare halves of one run, not runs.

## 8. Next

1. **The draw-time shader load as native code** (`8222BF10` with `8222A798`, the literal-constants writer `8222BDC8`
   and the two IM_LOAD packets): it runs on 95 to 100 % of the draws (the engine's command buffers set both shaders
   for every draw) and is now the largest part of a draw call (about 900 of about 1,400 A57 instructions). On the
   recording devices it takes one path almost always (flag 0x80: no in-place binding patch; the census shows IM_LOAD
   by address on 1.6 and LOAD_ALU_CONSTANT on 1.7 packets per call). Needs: the transliteration of that path, the
   in-place patch path (`822127F8`, with the vertex-fetch patcher called as it is) or a decline for it, and the same
   two-copy check (its regions are then compared too). Expected: about half of the remaining call.
2. **Records instead of PM4** only if the console A/B of `banjo_direct_state` (`configs/banjo_direct_ab.toml`) shows
   that the ring is faster with records there (on the PC it is 4 % slower). Then the native writers can publish the
   record in the same pass (they know the runs), which removes step 1's decode on the game threads; the ring side
   stays step 1's.
3. Smaller: BeginVertices / DrawVerticesUP (main device, 25 to 120 calls a Swap), the chunk loop of draws with more
   than 65535 vertices (0.3 % of the calls), the standalone FlushState `82222888` (17 calls a Swap).
4. Not measured, worth a look on the console: every draw hook calls `NoteDrawCall`, which with
   `banjo_no_draw_pairing` does one `fetch_add` on a counter shared by the three drawing threads (for a report line
   only). On the A57 a contended atomic on a shared line can cost more than this whole native path saves.

## 9. The API layer for performance: the plan from 2026-10-14, and its first operation (Clear)

The user's direction of 2026-10-14: an API replacement, for performance, built so that the other Xbox 360 ports can
take it. The draw path above changes only the game threads' cost. The frame rate in the console's level is set by the
GPU (36 % of the ring thread's samples wait for GPU fences, renut_003 / rex_profile 20 of 2026-10-14, stock clocks) and
then by the ring thread (about 40 to 50 us of work per draw). Writing host records instead of PM4 is not faster for the
ring here (section 3; the console A/B of `banjo_direct_state` again lost, 47.2 against 45.0 us per draw). So the
layer's order is by what it removes from the GPU and the ring, not by Direct3D's own structure:

1. **Clear as an operation** (below, done): Direct3D's Clear tells the renderer that it covers a whole surface.
2. **Resolve as an operation**: the resolve's source rectangle, destination texture and its clear, so the renderer
   copies surfaces, not EDRAM tiles.
3. **Surfaces instead of EDRAM tiles** where the operations above make it exact: render targets kept per Direct3D
   surface, EDRAM ownership only where the game really reuses EDRAM content (the 2x scene depth read as 1x).
4. **The draw path's state from the device, not from PM4** (draw records with the shader objects, textures and
   vertex buffers as objects), once 3 has made the renderer's per-draw work small enough for the transport to matter.

**What is per game and what is shared.** The renderer side (the ring's handling of the operations) is the same code in
every port of this family. Per game (per XDK build) are only: the address of the function hooked, the device offsets
of the bound targets, and the surface field that holds the size. For Banjo: the one-rectangle clear `82223560` (its only
caller is D3DDevice_Clear's per-rectangle step `822811A0`), colour target 0 at device +12440 and the depth surface at
+12456, the size in the surface's word +36 (width - 1 in bits 18-31, height - 1 in bits 3-17, as `822811A0` reads it).
SA2's clear is `82563AB0`, its targets at +12816 and +12832.

### Clear (`banjo_d3d_api_clear`, `app/src/me/banjo_d3d_clear.h`)

Direct3D clears a surface with a rectangle list; the renderer takes every EDRAM tile a rectangle covers whole without a
transfer. A surface whose width is not a multiple of the tile width (a 1024-wide shadow map at pitch 1040, a 96x96 4x
target at pitch 120, a 220x220 colour target at pitch 240) has a last tile column that the Clear covers only in part,
and the same for the last tile row. To stay exact the renderer copied the rest of those tiles in from their previous
owner on every Clear, with eight stencil passes when that owner had stencil. On the console's level (renut_003,
2026-10-14) every one of the top ten transfer pairs had Direct3D's clear shaders (VS643/PS267) as its consumer: about
140 transfers a frame, the EDRAM categories at 23.4 ms of GPU time a frame at stock clocks, with precise (end of work)
timestamps.

The hook on `82223560` writes a type-3 NOP before Direct3D's packets, `[BJCL, x1, y1, x2, y2, colour target 0 width |
height << 16, depth width | height << 16, flags]`, and `[BJCE]` after them, each only when the command buffer segment has
room before its soft limit (the same window test as the draw path). The ring keeps the last begin marker until the
end marker, or until a draw that is not a rectangle list. A rectangle of that draw is widened when the Clear's rectangle
is the whole surface (0, 0, width, height) of every view the rectangle overwrites, colour target 0 and depth only, no
window offset (tiling), and the rectangle reaches the surface's right edge (then to the pitch) or bottom edge (then to
the end of the tile row: 16 rows at 1x, 8 with MSAA). The widened rectangle is the overwrite rectangle the tile
ownership uses (and the stencil-replace rectangle when it was the same).

**The one assumption.** The pixels taken that way belong to no position of the surface: Direct3D leaves the EDRAM's old
content there, and after the widening they hold whatever the view's own image held. Nothing reads them through this
surface (its draws and resolves stay inside its width and height), and a surface that shares the EDRAM clears its own
area before it uses it. `banjo_diag_clear_padding = 1` tests that: the taken pixels are filled with magenta or a depth of
0.37 right after each widened Clear, so a read would show; with `banjo_ab_target = 20` the use of the markers switches
every few seconds, and `tools/ab_flicker.py` on a recording shows any difference that follows the switch
(`configs/banjo_api_clear_poison.toml`).

Report lines: `[d3d clear]` (game side: clears, markers written, no room) and `[native] Direct3D Clear markers` (ring:
markers, rectangles widened, the reasons for the others). In-run A/B target 20 also gives the `GPU A/B` line (both
halves' GPU time per Swap by category), new on 2026-10-14 for every A/B target.

**How Direct3D's Clear reaches the renderer** (the first test's per-kind trace): the whole tiles are cleared through a
4x alias of half the pitch (an 800-pitch shadow map is cleared at pitch 400, 4x, rectangle 0,0-360,384), the partial
edge column and row at 1x as a rectangle list. Only the 1x rectangles are widened; the alias rectangles are declined
("surface wider than the pitch": the surface size is in 1x pixels), as are clears whose depth and colour surfaces
differ in size. One more rule came out of the first test: a widened rectangle is drawn in its own view
(`banjo_clear_own_surface`). The renderer's redirected clear would otherwise clear those now whole tiles inside the
tile's previous owner (the previous shadow cascade), and the surface's next draw pulled the column back: 48 tiles a
frame, more than before.

**Results (PC, `tools/run_pc_test.sh` route, 340 s to the level, 2026-10-14):**

| Run | Result |
|---|---|
| Defaults | 64,820 clears, all marked (0 without room); 1,800 rectangles widened per 10 s report. EDRAM mode 4 in the level: **0 tiles transferred** in every report (before: about 6,900 a report, 23 a frame, the top pairs Direct3D's clear shaders); GPU per Swap: EDRAM import 0.3 -> 0.0 ms, export 1.8 -> 0.0, import9 0.2 -> 0.0. 0 DIFFERENCE, no crash |
| Poison (`banjo_diag_clear_padding = 1`, A/B target 20 every 2 s, 30 s recording) | about 1,000 padding rectangles filled per report; `tools/ab_flicker.py`: 15 switches in the recording, 14 at the background level (0.2-0.5 against a median of 0.30), the first in the recording's opening second, where differences of 6-9 come without any switch (1 of 15 above the 90th percentile, as chance gives). No magenta, no broken shadow in the contact sheet. GPU A/B of the same run: EDRAM import + import9 + export 0.2 ms per Swap with the markers used, 2.4 ms without |

### Steps 2-4: what each one has to deliver (the user's instruction of 2026-10-14: all four before the next console
### test)

Measured starting point (console, stock clocks, the level of renut_003): GPU 80-175 ms a frame (scene 33-120, EDRAM
23.4 before the Clear operation, copies 3.6, shadow maps 4-6, smaller targets up to 12, idle gaps 6-8); the ring
thread waits on GPU fences in 36 % of its samples, so its "work" figure (40-50 us a draw) includes that wait. Its own
cost per draw, from the stage timers (14.4 us timed on the console against 2.1 us on the PC, the rest on the PC 0.6 us):
about 20-25 us a draw at 1020 MHz, of it the index stage 2.7, BindPipeline 2.75, the driver's draw 2.4, textures 2.2,
the UBO set 1.3, PM4 and identification 5.2.

- **Step 2, Resolve as an operation.** A marker from Direct3D's Resolve (8222D6C8): source surface and rectangle,
  destination texture (object, level, point), and the clear flags and values. Uses: the clear after a resolve gets the
  Clear operation's rule (whole surface: edge tiles taken whole); the source is synchronized over the copied pixels
  only; the destination texture's shape is known before the copy. Expected: little GPU time on the PC route (no
  transfers left there); the console's resolve-caused transfers (about 4 a frame) and the information step 3 needs.
- **Step 3, surfaces.** The bound targets' Direct3D surface objects reach the ring (markers from the surface setters
  or the draw path's device read). A view whose EDRAM range is not shared with another surface since its last use is
  taken whole without any tile bookkeeping; a surface bound over another surface's EDRAM without a full overwrite is
  a reinterpretation the game asked for (the console level's 2x scene depth read as 1x, about 0.4 a frame, 720 tiles
  through the nine-pass import), done once per surface with the cheapest conversion that is exact.
- **Step 4, draw state from the device.** Objects instead of values where the ring re-derives them per draw: index
  buffers converted once per index buffer object and version (Direct3D's lock count, with the fingerprint check kept
  as the guard) instead of once per frame; textures and samplers by fetch-constant identity; the shader pair from the
  draw path's own bound objects (no identification by microcode). Target: the index, textures and identification
  stages, about 10 of the 20-25 us a draw.

### Results with the console's settings, Resolve, and step 4's first try (2026-10-14, evening)

**The PC test used the wrong settings until now.** The PC's renut.toml sets `native_2x_msaa = false` and few other
keys, so the PC rendered the scene without 2x MSAA and never showed the console's 2x/1x depth switches. With the
console's renut.toml on the PC (only the window and GPU keys added) and a route that lets the intro cutscene play
(A only through the title and menus), the PC reproduces the console log's transfer pairs one for one. Every PC
measurement from here on uses the console's settings.

| Intro cutscene, console settings, PC | transfer tiles a frame | EDRAM GPU a frame (PC) |
|---|---|---|
| Neither operation | about 87 | about 3.8 ms |
| Clear operation | about 24 (all of them the clear after a Resolve of the 96x96 4x target) | 0.3 ms |
| Clear + Resolve operations | 0 | 0.3 ms (the alias category) |

**Resolve (`banjo_d3d_api_resolve`, A/B 21).** The 24 tiles left were not a Clear: ClearDepth (the clear of a
resolve) of the 96x96 4x depth surface runs in the raster-grid view and imported the partly covered border column
(pitch padding: 96 of 120) from the normal 4x view. The hook on D3DDevice_Resolve (`8222D6C8`: r4 low 3 bits pick colour
target 0-3 at device +12440 + 4 i, or 4 = depth at +12456; r5 the rectangle or 0) writes `[BJRS, surface width |
height << 16 when the rectangle covers it whole, else 0]` and `[BJRE]`. The ring copies the size into
`RegistersCopy::banjo_surface_size`, and `GetPlanClearedEDRAM4` widens a whole-surface resolve clear to the pitch and
the end of the tile row, as the Clear operation does. 201,028 of 210,736 resolves cover their whole surface.

**Dead draws (`banjo_gpu_skip_dead_draws`, A/B 22, from sasrt-nx).** The ring answers occlusion queries itself, so a
draw that writes no colour, depth, stencil or memory changes nothing; it is skipped before its EDRAM preparation.
254,000 per 10 s in the intro (about 850 a frame). PC: no change in ring or GPU time (vsync-bound, no transfers left
for them); the A/B recording shows nothing that follows the switch; verify mode drew 253,154 of them and the
renderer's own write analysis published nothing for any. The console pays each of them in the driver (about 2.4 us a
draw at stock) and on the GPU.

**Step 4's first try: Direct3D Locks instead of re-reading vertex memory (`banjo_ring_vertex_lock_rule`), NOT
exact, OFF.** The game threads note every D3DVertexBuffer_Lock / D3DIndexBuffer_Lock by 4 KB page (`banjo_d3d_locks.h`);
a census over the intro found about 217,000 re-fingerprints of persistent vertex blocks per 10 s, none of a block
changed without a Lock (226 Locks in the whole run, all at loading). With the rule on, 29 s after start a 640 KB block
was rewritten with no Lock of its pages, and the persistent path's own byte-for-byte check caught it (and switched the
persistent vertices off for the session). Loading writes vertex memory some other way (file reads, decompression). So
object versions from Direct3D's API are not a proof for this game's vertex memory; the census kept running only
showed which blocks happened to be judged twice. The page record stays as a diagnostic.

### Step 4 (2026-10-15, night): what was done, measured and checked

Console starting point (sixteenth package, stock, intro cutscene): the ring thread about 26 us a draw, the limit
there (the GPU idle 16 ms a frame); its stages: textures 4.15 us, indices 3.13, pipeline bind 2.88 (driver), draw 2.52
(driver), UBO set 1.40 (driver), pipeline lookup 0.84, shared constants 0.77; PM4 and identification 4.4.

- **Sampler cache range invalidation (`banjo_ring_texture_invalidate_range`, A/B 24).** 36,003 of the 37,052 table
  misses per 10 s were whole-cache clears by a resolve (about 21 a frame). A resolve now clears only the entries whose
  texture memory (base level and mips, 16 bytes a texel on 32x32 tiles: never smaller than the real footprint) can
  overlap the resolved range; the scan walks only the slots filled since the last whole-cache clear. PC: misses 33 ->
  11-14 %. Verify: 1,932,793 kept entries prepared again and compared, 0 differences.
- **Persistent indices (`banjo_ring_index_persistent`, A/B 25).** The console converted and copied about 9 MB of
  16-bit indices a frame (the per-frame cache hit 8 %). Index ranges now keep their converted copy in the persistent
  arena under the vertex blocks' proof; the arena can be bound as the index buffer (rebound only when the buffer
  changes). PC: 146,000 draws per 10 s from the arena, 798 MB per 10 s neither converted nor copied; verify: 5,967,140
  uses converted again and compared, 0 differences. The arena used 45 of its 64 MB.
  Fixed on 2026-10-15 (found by the sasrt-nx port): the index stage runs before the vertex stage, which is where a new
  synchronization point (a guest wait) used to end the interval. The first draw after a wait could therefore take an
  index block as proven by the interval that had just ended. Both stages now call `PersistSync()` first. The verify
  runs had re-converted every use and never met a stale one, but the order was wrong.
- **Not done, and why:** records instead of PM4 (lost the console A/B twice); vertex hashing replaced by Direct3D's
  Locks (not exact, see above); the resolve image swap (`masseffect_native_resolver_no_copy`) crashed the game on the
  PC within minutes.

### Step 3 (2026-10-15): where it stands

With the Clear and Resolve operations, the console's intro cutscene has no EDRAM transfers left; the EDRAM draw memo
already prepares 92-93 % of the draws in constant time (surface-level, by the bound views). What remains in the EDRAM
GPU categories (about 3.2 ms a frame at stock) is the work any renderer does: about 6 depth resolves of the shadow maps
into textures and about 5 depth clears (2,380 tiles) a frame. Rewriting the tile model into surfaces would change the
architecture without saving frame time now, so it is not done; the next frame time is in the scene's shaders.

## Files

New: `app/src/me/banjo_d3d_native.h`, `app/src/me/banjo_d3d_census.h`, `tools/d3d/ppcfn.py`, `tools/d3d/d3d_writers.py`,
`tools/d3d/d3d_census_build.py`, `tools/d3d/d3d_census_report.py`, `tools/d3d/d3d_arm_lines.py`, this file.
Changed: `app/src/me/banjo_me_hooks.cpp` (includes, the three draw hooks, the shader load and Swap hooks' census
calls, the 10 s report, the include of the census hooks file when it exists), `app/src/cpu/banjo_enhancements.cpp`
(the two keys' rows), `app/src/me/me_verify.h` (target 19 in the comment), `app/src/me/me_native_system.cpp`
(`banjo_ab_target` up to 17 and its label). Generated code: `tools/post_codegen.py` must run once after the merge
(the two helper addresses named above).

## Merged into the main tree (2026-10-14)

Built in the copy `banjo-nx-r` and merged by hand into `banjo-nx`. There the in-run A/B target is **19** (16 is
deferred recording, 17 `banjo_gpu_proof_depth_only`, 18 `banjo_gpu_edram4_split_tiles`); the measurements above were
taken with it as target 17 in the copy. The main tree had moved on meanwhile (deferred recording switchable in the
run, the depth-only overwrite proof, split EDRAM tiles); none of that touches Direct3D's draw functions.
