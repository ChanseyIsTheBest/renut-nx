# Render-thread work for other ports and the base (banjo-nx-ult, 2026-11-20/21)

What banjo-nx's render-thread rounds of 2026-11-20/21 measured on the console (Showdown Town, handheld, stock clocks,
4-core mode), for the base and for other ports to test. Everything is in this repository (D:\Banjo kazooie
port\banjo-nx-ult, local commits; `git show <hash>`), each item behind its own key and in-run A/B target. Details and
every run: docs/banjo-ultimate-notes.md, "The render-thread batch" and "The render thread to ~33 ms".

## How it was measured (read this first)

- **In-run A/B only.** `banjo_renut_ab_targets = "a,b,c"`, `banjo_renut_ab_s = 8`; judge each target by the **median
  of its pairs** of 8 s halves (tools/ab_pairs.py LOG: render thread, GPU and all-threads CPU a draw, pairs with "on"
  higher). tools/ab_halves.py's totals let one slow half (a hitch, a compile) swing a target by 5 %.
- **Runs of the same NRO differ by 2-4 %** between console sessions (same scene, same draw count). Compare whole builds
  only in the same session, and only within one scene state (N&B's town is day or night per run, decided at load).
- **Every hot change needs a PGO retrain** before judging it (a stale profile costs more than most items save).
- **On the A57 the render thread waits on memory.** A line's share of a sampled profile is not what removing it saves:
  the miss moves to the next touch of the same line. Profile shares overstated every exact item by 3-10x.
- **Prefetches that need a load to find their address cost.** Prefetches from a known pointer or the record's own
  words pay when they are few lines; five lines a buffer an op cost again.

## Send / test (measured wins, or clear per-game trades)

**SASRT's check (2026-11-21, sasrt-nx runs 340-344: 4 players still at the start, ~3,500 draws a frame at ~17 us,
18-22 pairs a target):** 174 -0.35 %, 175 -0.06 %, 56 -0.06 % (none of the three prefetches costs there), 173 -0.24 %
render / GPU +0.03 %, 179 kick submits +1.28 % render thread a draw but all-threads CPU -1.08 % and fps +0.2 % (kept
on there). So the prefetch removals are N&B's (5,000 draws at ~8 us, and N&B's PrefetchShaderBehind has the later
stage-skip), not universal: each port measures them on its own scene before taking them. sasrt-nx's write-up:
D:\sonic transformed\sasrt-nx\docsender-thread-ideas-1120.md, "Round 5".

| What | Key / A/B | Console result (render thread a draw, item on vs off, pair median) | Commit |
|---|---|---|---|
| PrefetchShaderBehind (Replay: at a shader change, the record's entry and declaration lines) **off** | banjo_renut_replay_shader_behind (off), A/B 174 | +1.21 % / +1.57 % with it (17/17, 19/21 pairs higher), GPU 0 | 6a90503d |
| ApplyResources' copy and card prefetch **off** | banjo_renut_replay_copy_prefetch (off), A/B 175 | +0.12 % / +0.58 % with it | 6a90503d |
| PrefetchResourceObjects (next op's texture and geometry objects) **off** | banjo_renut_replay_object_prefetch (off), A/B 56 | +0.36 % / +0.24 % with it (third run -0.03 %) | 79a05005 |
| Kick submits: the frame to the GPU in pieces at the game's kicks | banjo_deko3d_kick_submit, A/B 179 | **+1.74 %** with it (18/18), GPU +1.65 % a draw, fps -2.3 %. Buys input latency: a per-game trade. N&B: off on the Switch (the user) | 39354bba, cf8b37ac |
| Command memory CPU-cached, cleaned at the list's end | banjo_deko3d_cached_commands, A/B 173 | render +0.11 % / +0.90 %, **GPU -1.0 % / -0.9 %** a draw, all-threads CPU -0.9 %: on for a GPU-bound game, off for a render-thread-bound one (N&B: off) | 849e7a3f, 8c90ee87 |
| The look-ahead's key tested before the next op is read | - | (a load less a draw with the look-ahead off) | 6a90503d |

Prefetches measured as paying (keep them): Replay's next-op shader memo sets (A/B 176, -0.36 %), the fast path's
shader-change prefetches (177, -0.60 %) and texture-slot / pipeline-entry prefetches (178, -0.36 %), the next op's
state lines (55, -0.30 % / -0.39 %).

## Measured and not worth sending

| What | Result |
|---|---|
| Spec answers kept while their inputs hold (A/B 172; cc0cc81a, 8d9cb5c5) | -0.23 % / +0.29 %: noise. Its own bookkeeping (0.8 % of samples) eats what it saves |
| Store cards prefetched an op ahead from the record (A/B 181; 39354bba) | +0.31 % with it: off (79a05005) |
| The renderer and plume with -fomit-frame-pointer (3d554cc0) | no visible change; the stack samples lose the callers |
| The 80-byte spec-word compares inline instead of memcmp (849e7a3f) | no visible change |
| The 44-item batch with all of sasrt-nx's rounds 2-4 (a390d9b2, 6a8d3760, 430bb3c4) | ~8-10 % **slower** a draw than its parent: spec memo bookkeeping, byte-atomic On() checks, a struct zeroed per draw, the render thread waiting for the lowest-priority ring cleaner. Round 2 (08b1a669) fixed those; the lean batch (a8b56d4a: the parent plus only what measured or costs nothing) equals the parent a draw |

## In progress

- The deko3d command stage (cf8b37ac; banjo_deko3d_command_stage, A/B 182, off by default): package 28b's two-thread
  recorder on deko3d, its thread on the system core in 4-core mode: plume's command list and libdeko3d's encoding off
  the render thread (12-14 % of it in N&B). Not measured yet; the notes will have it.
