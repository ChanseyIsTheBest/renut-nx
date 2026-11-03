# Performance enhancements and their switches

The reference pages: `docs/enhancements-reference.md` (every run-time key: what it does, why it is exact, how it is
checked, what was measured) and `docs/codegen-enhancements.md` (the build-time changes to the generated game code).
This page is the history of the CPU work that still runs, and of the renderer rounds it shares with them. The earlier
PM4 renderer's rounds are left out; the deko3d renderer's own record is `docs/deko3d-renderer.md`.

Every enhancement can be turned off from `renut.toml`. An enhancement is active only when its own key **and** its
group's master switch are on. The log has one `[enhancements]` line at startup that lists each one and whether it ran
(`src/cpu/banjo_enhancements.cpp`).

| Master switch | Default | Covers |
|---|---|---|
| `banjo_cpu_enhancements` | `true` | host CPU work: guest-code replacements, waits, thread usage |
| `banjo_noncpu_enhancements` | `true` | GPU and storage side |

## Verify configuration

`banjo_cpu_verify = true` (`configs/banjo_cpu_verify.toml`) runs every CPU enhancement and checks each against its
original path on **every** use (the self-check counts are raised from 2048 to 2^31-1). The renderer's 10-second report
then has a line

    [verify] checks done: ... | differences: D

**D must be 0.** Every DIFFERENCE / MISMATCH report and every native game-function mismatch counts into it
(`src/me/me_verify.h`). It is slower (both paths run), so it is for a test session, not play.

## Profiling

Off in the release (logging is off). Plain keys, outside the enhancement master switches:

- `switch_profile_stacks = true`: the SDK sampler (`sdk/src/ui/switch_perf.cpp`) pauses busy threads 1000 times a
  second and writes, every 10 s, every pc with its count to `logs/rex/rex_profile.log`.
  `python tools/switch_hot_functions.py rex_profile.log --map <build>/renut.map` ranks functions by self time.
- `renut_gpu_profile = 10`: the GPU's time per pass, as `GPU profile` lines; `banjo_renut_diag_gpu_shaders` splits it by
  pixel shader. The image does not change.
- `banjo_profile.toml` also turns on the app's own sampler (`banjo_cpu_profiler`, `logs/rex/banjo_profiler.log`,
  `tools/switch-cpu-profile.py`).

Use the ELF and map of the same build as the NRO (`builds/<date>/`).

## Build-time changes (A/B by files, not keys)

| Change | How to A/B |
|---|---|
| `-fno-math-errno` on the generated game code (`BANJO_GENERATED_MATH_FLAGS`, masseffect-nx ships it, futari-nx checked it): no errno bookkeeping in math functions (sqrt is one instruction). IEEE results unchanged (no finite-math). **`-fno-trapping-math` was dropped on 2026-10-12** (sa2-nx's audit): it lets GCC run a float operation earlier than the code does (out of a branch or loop), and the recompiled code switches the host's flush mode with `msr fpcr` in between, which the compiler does not treat as something a float operation depends on, so an operation moved across a mode switch could treat a denormal operand in the wrong mode. Nobody observed a wrong result; the reasoning is sa2-nx's, and the flag bought sa2-nx at most 0.5 % of one file. Packages up to 2026-10-07_1659 were built with it. | `-DBANJO_GENERATED_MATH_FLAGS=OFF`; `-DBANJO_GENERATED_NO_TRAPPING_MATH=ON` puts the dropped flag back for a measurement. |
| **Optimised codegen** (2026-10-07, from sa2-nx, which took it from masseffect-nx): `app/renut_config.toml` keys `cr_as_local`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `skip_msr`, `dform_disp_split`, `fctiwz_inline` (guest condition/count/XER registers and the lwarx reservation as C++ locals, no MSR fences around guest atomics, fctiwz inline), and the post-codegen steps of `tools/post_codegen.py`: guest loads/stores without `volatile` (`pch_no_volatile.py`, masseffect-nx measured +15-20 % fps on the console) with a compiler barrier at every loop header (`loop_barriers.py`, REQUIRED: without it GCC turns guest polling loops into infinite loops), and direct calls between unhooked functions so GCC can inline them (`direct_calls.py`: 129,188 calls, 466 hooked addresses kept). `app/CMakeLists.txt` re-runs the steps after every codegen. The guest-visible result is the same; only the shape of the C++ changes. `tools/find_self_loops.py <elf>` must find no self-loops in recompiled functions (since the CPU rounds of 2026-10-13: none but `sub_82C25230`, explained in that section). | One NRO only (user rule): remove the keys from `renut_config.toml` and rebuild (the build regenerates and drops the steps). |
| **`fp_moves_modeless`** (2026-10-07, codegen option from sa2-nx's SDK round 7, copied into `banjo-nx/sdk`): a guest floating-point load, store or move no longer switches the host FPU between the scalar (IEEE) and VMX (flush-to-zero) modes with an `msr fpcr`; `lfs`/`stfs` convert with a helper that gives the IEEE result in either mode. Only arithmetic switches modes now. The guest-visible result is the same. Side effect: a function can now return in flush mode where its old epilogue `lfd` switched it back, so the native reverb (`banjo_reverb_native.cpp`) leaves the mode as the generated reverb does (`BANJO_FP_MOVES_MODELESS`, set by `app/CMakeLists.txt` from `renut_config.toml`); its verify compares the mode bit-exactly: 20,500 calls, 0 mismatching, whole verify session `differences: 0`. Switch ELF: `msr fpcr` 40,120 → 30,988 (−22.8 %), .text +553 KB (+0.9 %, the denormal-safe conversions inline); no self-loops in recompiled functions. | Set `fp_moves_modeless = false` in `renut_config.toml` and rebuild. |
| **`non_volatile_as_local` + `elide_lr_stores`** (2026-10-07, the other ports' measured "diet": masseffect-nx -10 % main thread, sa2-nx round 7, sasrt-nx): r14-r31, f14-f31, v14-v31 and v64-v127 become C++ locals, so the `__savegprlr`/`__restgprlr`/`__savefpr`/`__savevmx` helpers (35,300 static calls in the Switch ELF) and the guest-stack traffic for those registers disappear, and `ctx.lr = <return address>` is no longer stored before every call. Switch ELF: 0 helper calls left, .text 64.71 → 61.42 MB (−5.1 %), NRO 66.1 → 62.8 MB (builds/2026-10-07_0851_optimised); PC: renut.exe 64.4 → 56.4 MB; the verify session checked 2.5-4x more draws and audio blocks in the same 200 s. Audit (`app/renut_config.toml` has the details): (1) **setjmp**: 10 guest functions call it; `banjo-nx/sdk`'s codegen now copies every C++-local register into `env` at a setjmp and back after a longjmp (`function_graph.cpp`, `builders/context.cpp`), as the guest's longjmp does. (2) **ctx.lr** is read only by diagnostics; the guest unwinder is not implemented. (3) **`tools/read_before_write.py`** had two blind spots inherited from sa2-nx, now fixed: it never read named functions (`rex_*`; the CRT's SEH `__finally` funclets: `rex_LN15_18` crashed the first boot) and it dropped every vector read (every vector instruction writes through `simde_mm_store*`, which it took for a memory store). Fixed, it found: 70 new `share_registers` marks (39 fragments without a frame, 31 SEH funclets; a mark replaces the include's entry, so it repeats the name); and **registers this game uses outside the ABI**: VMX128 constants in v64-v77 built once by `sub_8239CE88` and read by ~900 functions (Microsoft's WMV decoder hands its asm routines permute masks in v64/v65 the same way: with locals every FMV was garbage on both renderers), and an implicit argument in v92 (`sub_82BD9408` → `sub_82BD8D68` → `sub_82BD59F8`). New SDK codegen keys: `non_volatile_as_local_keep = ["v64-v77", "v92"]` (those registers stay in ctx in every function; a save/restore helper covering one is called) and `non_volatile_as_local_exclude = [[start, end], ...]` (whole asm libraries keep the old code; the helper bodies keep ctx for them; used only for bisecting here). Residue, harmless: 26 normal functions whose own prologue saves the register they read early (infeasible paths through jump tables in loops; a read of an uninitialised variable would get 0 instead of the caller's leftover), data words in front of 491 functions, xor rX,rX,rX. Checked: verify session `differences: 0` (native reverb 63,000 calls bit-exact), videos of the FMVs, title, menus, the intro cutscenes and Spiral Mountain gameplay; no self-loops in recompiled functions. | Set both keys to false in `renut_config.toml` and rebuild. |
| **`fcti_inline`** (2026-10-12, codegen option from sa2-nx's second CPU round, copied into `banjo-nx/sdk`: `rex/ppc/diet_ops.h`, `builders/floating_point.cpp`): `fctid`, `fctidz` and `fctiw` through `rex::ppc` helpers instead of an isnan test, a range compare and simde's scalar conversion around each (Banjo: 425 `fctid`, 599 `fctidz`, 0 `fctiw`). **A correction for the Switch, not a measurable speed-up there (one compare fewer per site):** off x86, simde's `simde_mm_cvtsd_si64`/`si32` round with C `round()`, exact halves AWAY from zero (0.5 → 1, 2.5 → 3), where the Xbox 360 and the PC build round in the current mode, halves to even (0, 2). So every Switch build before this one rounded exact halves differently from the 360 and from the PC at the 425 `fctid` sites (310 of them are the `fctid` + `fcfid` "round to an integer" pair). The helper is `frinti` (current mode, FPSCR.RN is mirrored into FPCR) + the saturating `fcvtzs`. It also fixes simde's `si32` path answering INT32_MIN for a value that rounds up to exactly INT32_MAX. On x86 the result is unchanged except at exactly 2^63 (INT64_MAX as on the 360; `cvtsd2si` gave INT64_MIN). Self-check: every boot runs the helpers on 15 inputs (the halves, saturation, NaN) and logs `[enhancements] float-to-integer rounding self-check: 45 of 45 results as the Xbox 360's`: the AArch64 path cannot run on the PC, so the console log is its proof. Checked 2026-10-12. PC: `tools/read_before_write.py` output identical to the previous build's (535 lines); verify session `differences: 0`, 0 rejected draws; a 290 s recorded run (title, intro cutscenes, Spiral Mountain) normal. Switch ELF (builds/2026-10-12_2130_rounding against 2026-10-07_1659_sa2port): `fcvtas` (convert, ties AWAY: what GCC made of simde's `(int64_t)round(v)`) 435 → 12, none left in recompiled functions (the 12 are libm and Mesa); `frinti` 1 → 425, 422 of them in recompiled functions; no self-loops in recompiled functions; NRO size unchanged (62,896,325 bytes). Not yet run on a console. | Set `fcti_inline = false` in `renut_config.toml` and rebuild. |

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

## Floating point in the generated code (a correctness fix, not an enhancement)

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

## Early alpha test (`tools/early_alpha.py` through `tools/renut_early_alpha.py`; build-time, `BANJO_RENUT_EARLY_ALPHA`)

Nuts & Bolts' shadow-receiving materials each run three shadow cascades of 8 taps and a light loop, then an alpha test at
the very end. Their alpha needs only the first one or two fetches: the shadow sum is added to it times a run-time constant
that is 0 in those draws, or times a literal 0 of the shader's own constants.

`tools/renut_early_alpha.py` runs `tools/early_alpha.py` on the HLSL of every pixel shader, between two runs of Renut's
translator. It slices main() backwards from the alpha clips (per component), finds the earliest top-level point after
which that slice has nothing it cannot repeat, and emits `banjoEarlyAlphaFails(<registers live there>)`, a copy of the
slice, plus a call at that point under `g_AlphaTestFunc != 7u` that discards a 2x2 quad whose four invocations all fail
(`banjoEarlyAlphaQuad`: fine derivatives of a 0/1 value). The copy tests the draw's alpha function and reference at run time
(`banjoAlphaFailsEarlyRt`); on deko3d the function is a constant of the pipeline (`docs/deko3d-shader-optimisations.md`,
2.3).

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
  differs once keeps none (`renut_early_alpha.txt` lists every shader and why).

3,663 of 3,936 pixel shaders have a copy (197 have too little after their alpha to gain, 76 use forms the tool does not
read); none differed.

## PMULL hash (`banjo_renut_pmull_hash`; CPU group; ARM only, no in-run A/B)

Renut's per-frame content hashes of guest data (vertex streams, index ranges, texture samples, CPU-written textures) use a
PMULL-folded CRC-64 instead of XXH3: half the instructions per byte on the A57. The shader containers keep XXH3 (the cache
is keyed by it).

`banjo_pmull_fingerprint.h` folds a CRC-64 (ECMA-182 polynomial) with PMULL / PMULL2 instead: four independent chains
fold every fourth 16-byte block by x^512, are combined, the tail is folded block by block, and a Barrett reduction
gives the exact residue; the seed is added to the first block and the length enters a bijective final mix. Its main
loop (checked in the Switch object file) is 8 PMULL, 2 LDP and 8 EOR per 64 bytes, about 8 cycles. A change spanning fewer than 64 bits of a block is always seen, other changes escape
with probability 2^-64.

Checks: `sdk/tests/pmull_fingerprint_test.cpp` (`tools/run_sdk_tests.bat pmull`), through simde's model of the same
intrinsics: 2,811 lengths and seeds against the bit-by-bit reference, 0 differences; every one of 28,192 single- and short
two-bit changes of a 1 KB message seen. At start a self-test compares it with the bit-by-bit reference, and any difference
keeps XXH3.

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

## Package 26 (2026-10-16/17): Renut's renderer

The user's overnight mandate of 2026-10-16: Renut upstream's Direct3D-level renderer built into the NRO, on the Switch,
and tuned for the A57 with banjo-nx's own measured items: constants through uniform buffers, branchless select, clamp bounds from the constant bank and
predicated blocks without a branch in Renut's translator; thread placement (`banjo_ring_core`, `banjo_background_compile_core`, `banjo_prewarm_threads`);
`banjo_renut_upload_cached`, `banjo_renut_pmull_hash`;
the `[renut]` 10 s report and its in-run A/B (`banjo_renut_ab_target`). The codegen improvements and the CPU group's
game-code items are the game's. Everything about it: `docs/renut-renderer-port.md`.

## Package 27 (2026-10-17): the rest of Renut's renderer, and a codegen round

The user's mandate: port to Renut's renderer every banjo-nx item it did not have yet (16 shader translator keys), make it
safe on the console's memory (no texture eviction glitches like sa2-nx's), and a round
of 15 aggressive codegen ideas, audited, the ones that hold implemented.

**Renut's renderer** now reads `banjo_gpu_bool_spec`, `banjo_gpu_blend_skip` and `banjo_gpu_tess_grid` (its in-run A/B targets 7-9 for the last three);
its translator has banjo-nx's literals from the constant bank, bone rows in one load, bone fetches without their branch,
the moves as their operand and early alpha (3,663 of 3,936 pixel shaders). The other eleven translator keys have
nothing to do there (each checked over the whole library: no `switch (pc)` loops, no texcoord swap, no texture sign
codes, the input assembler remaps, linked stages). Exactness and memory, not enhancements: `banjo_renut_exact_streams`
(cached vertex and index copies hashed at their first use after each kick), `banjo_renut_memory_policy` (console-sized
limits, cold textures only, never one the GPU wrote).
Details and counts: `docs/renut-renderer-port.md`, section 7, and `docs/shader.md`; the keys: `docs/enhancements-reference.md`, "Package 27".

**The codegen round** (it is the game's code): VMX vsel as one NEON bit select, the console profile's
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
