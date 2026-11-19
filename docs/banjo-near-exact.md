# banjo-nx: the not-exact forms and their censuses

The base's faster forms that are not bit-exact (docs/ultimate/off-by-default.md section 2), each checked on N&B before
it is turned on: the PC build counts, while it runs the exact forms, what each form would change
(`banjo_diag_runtime_census = true`, the "[census]" lines every 10 s). Each census is a PC run in gameplay (Showdown
Town on foot and in the vehicle; the route in docs/banjo-ultimate-notes.md), the windows before gameplay (boot, menus,
the load) left out of the verdict but listed.

**Rule (the user's):** a form is on only with its census done and written here; a form whose census counts stays off
until the user decides, with its sites below.

| Item | Switch | Census (gameplay windows) | Sites | Verdict | In the build |
|---|---|---|---|---|---|
| CG-13 | `BLUR_FPCR_ONE_MODE` (CMake; ON on the Switch since port 133) | 58 of 60 windows (cg13_town2): at most 608 FPU regions, 2,493 any-mode cold paths a second | 100 listed: the scalar ones all audio (XACT's engine and RPC, XAudio's router and effect chain, princeton_digital's reverb, the vehicle's sound effects, X3DAudio, the codec range) or stack temporaries read as vectors; the rest VMX ("on": flushed on the 360 too) | harmless: decaying audio signals and parameters, no division, normalisation, zero guard or simulation | **ON** (the base's Switch default) |
| #1 signalling NaNs at float copy stores | `REX_NX_SNAN_PASS` (BLUR_NEAR_EXACT) | 0 in every window (two runs, 104 windows) | none | cleared | on in the cleared set |
| #2 NaNs into float-to-integer conversions | `REX_NX_FCTI_NO_NAN` (BLUR_NX_ONLY) | 0 in every window (Blur: 80 a second) | none | cleared | on in the cleared set (by BLUR_NX_ONLY) |
| #17 divisions by zero, INT_MIN / -1 | `REX_NX_DIV_NO_CHECK` | 0 signed, 0 INT_MIN / -1, 0 unsigned | none | cleared | on in the cleared set |
| #41 float compares with a NaN | `REX_NX_FCMP_NO_UN` | **12**, all in the boot (between the second and third report), 0 in gameplay; the same 12 in both runs | not listed by the census (it counts, it does not record where) | counts: off | off |
| #42 vector max/min lanes that differ | `REX_NX_VMINMAX_FAST` (BLUR_NX_ONLY) | **956,781 of 393.6 million lanes** (0.24 %, run 1), 472,260 of 529.4 million (0.09 %, run 2) | not recorded | counts: off | off |
| #5 integer compares copying XER[SO] = 1 | `REX_NX_XER_SO_ZERO` | 0 in every window | none | cleared | on in the cleared set |
| #19 traps only log | `REX_NX_NO_TRAPS` | no trap line in either run | none | cleared | on in the cleared set |
| #9 r1 assumed preserved | `BLUR_NX_R1_ASSUME=1` (post-codegen) | the proof (r1_preserve.py on this tree): 31,381 functions proven with indirect calls taken as preserving; `--near-exact` assumes 8,143 more (39,524), 390 left (25 setjmp / longjmp, the rest r1 writes the flow cannot follow). The check: REX_R1_CHECK (PC verify) tests every assumed call | the 8,143 (out/census/r1_near_exact.json) | see the verify run below | |
| ABI trust in the copies | `BLUR_LOCALIZE_ABI_TRUST=1` (post-codegen) | | | | |
| Stack-frame elisions | `BANJO_GEN_LR_MOVES_ELIDE`, `_NV_SAVES_ELIDE`, `_BACK_CHAIN_ELIDE`; `SASRT_HOT_LR_ELIDE` | | | | |
| No trapping math | `BANJO_GENERATED_NO_TRAPPING_MATH` (CMake; the Switch's GCC only: clang on the PC assumes no traps already) | not a run: a code census. app/generated/fp_env_files.txt lists the 5 files that write the guest FPSCR: `_ctrlfp`, `_set_fsr`, `_FPreset`, `_set_statfp`, `JumpToContinuation` (the CRT's; cg13_town1 counted 34 writes, 10 to a mode other than nearest); they keep -ftrapping-math (the CMake form grafted from blur-nx: the base's switch put the option on every file, the writers too). The 5 readers (`loadFromHost`: `_get_fsr` and the same CRT functions) return the rounding mode only, never the host's exception flags, and no game code enables a host trap; under CG-13 there is no flush-mode switch left for an operation to move across | the 5 writers | cleared (exact under CG-13: what it lets GCC move changes no value the guest sees) | on in the Switch build (text size below) |

## Runs

### cg13_town1 (2026-11-19 12:42, PC, 460 s: the menus, then Showdown Town in the vehicle with the sticks wandering)

`tools/cg13_census.ps1 -Seconds 460 -Sticks 160` (CG-13 OFF in the PC build, the census on). 44 windows of 10 s; about
1.09 million flush-mode switches a second in the game code (each an `msr fpcr` the Switch build leaves out).

**CG-13:** 42 of 44 windows met denormals: at most 246 FPU-mode regions and 2,493 any-mode cold paths a second. The
sites (the top 48, cumulative), by function:

| Share | Site | What it is |
|---|---|---|
| 76.2 % | copy of sub_8220DDE8 | XACT's engine (between `CCue::GetVariable` and the heap's `RtlpCoalesceFreeBlocks`, under XACT's critical section, reached by pointer): its parameter updates |
| 5.9 % | `XAUDIO::CRouterEffect::Process` (copy) | XAudio's router (the mix to the outputs) |
| 5.3 %, 0.2 % | copies of sub_82C354A0, sub_82C395E8 | XAudio's effect chain: called by sub_8298F3E8 (with sub_82C3D4E8, sub_8296FFF0 and sub_82C41318), among XAudio's frame buffer, the XMV player and voice chat; next to princeton_digital's reverb (`threetap_t`) |
| 6.2 % | sub_8296FFF0 (vmsum3fp128, vcmpgtfp, vmulfp128: VMX code) | the same chain, its vector code |
| 4.4 % | sub_82C3D4E8 (fmuls / stfs, vmulfp128, vsubfp128) | the same chain |
| 0.2 % | copy of sub_827496C0 (two `stfs` of doubles to the stack) | game code; the six floats at r1+96 and r1+112 are then loaded as two vectors (lvsl-based loads at loc_82749C18): the VMX flushes them anyway |
| 0.1 % | sub_82C41318 (vmulfp128, fmuls, vsubfp) | the effect chain (behind the thunk sub_82C42018) |
| < 0.1 % | copies of sub_82211B58, sub_82C395E8 | XACT / XAudio calls from game code (sub_82629B08, sub_82610AF0) |
| ~1.5 % | `banjo::FormsSelfCheck` | its own deliberate denormals at start-up |

Nearly all in the audio engine's mixing and effects (decaying signals and parameters: the classic denormal source, a
value that becomes zero), the rest VMX-consumed temporaries; no division or normalisation of such a value, no guard
against zero, nothing of the vehicle's or the player's simulation in the top 48. The report printed only the top 48
sites of a 16,384-entry table, and the self-check's took about 20 of them, so a rarer site could have been left out:
widened to 256 (app/src/cpu/banjo_codegen_checks.cpp) and run again (cg13_town2, longer, with the vehicle left).

### cg13_town2 (2026-11-19 13:05, PC, 620 s: the same, Y / B / A presses from 250 s: Banjo on foot and back in)

60 windows; 1.12 million flush-mode switches a second. **CG-13:** 58 of 60 windows (at most 608 FPU regions, 2,275 cold
paths a second), 100 sites listed. Besides the first run's: the scalar ("?") sites sub_8234BB28 (234; next to
`entityAvatarBuggySfxRemoveCue`: the vehicle's sound effects), sub_829ADEC0 (117; the codec / voice range, its callers
sub_82959AA0, sub_82999F30 ...), sub_82936F20 and sub_8233DA50 (they call the audio chain's helpers sub_8291Bxxx and
sub_8296FBA0), sub_822C6138 (11; calls sub_822037C0, right after `X3DAudioCalculate`: positional sound),
sub_8222F988 and sub_8222C410 (a few; XACT's range, sub_8222C410 reaches sub_82211B58); the vector ("on") sites
(vmulfp128, vmsum3fp128, vsubfp, vmaddfp in the audio chain and in sub_822D1358, sub_822C6138, sub_821C0940 ...) are
VMX results, which the Xenon's VMX flushes as well. **Verdict: harmless; CG-13 stays ON in the Switch build.**

**Near-exact:** the same as run 1: #1, #2, #17, #5 0; #41 the same 12 in the boot; #42 472,260 of 529.4 million lanes;
no trap.

**The cleared set:** `-DBLUR_NEAR_EXACT=ON "-DBLUR_NX_ONLY=REX_NX_SNAN_PASS;REX_NX_FCTI_NO_NAN;REX_NX_DIV_NO_CHECK;REX_NX_XER_SO_ZERO;REX_NX_NO_TRAPS"`
(#1, #2, #17, #5, #19; not #41, #42).

**Near-exact (cumulative at the end):** #1 0, #2 0, #17 0 (signed, INT_MIN / -1, unsigned), #41 **12** (all between
the second and third report: the boot; 0 after), #42 **956,781 of 393,594,997** lanes (0.24 %), #5 0; no trap line
(#19). Evidence for the exact items: #3 FPSCR writes 34 (20 changing the mode, 10 to a mode other than nearest), #4
FPSCR reads 34; #38 card marks 4.81 billion (the copies' run marks 1.79 billion, 307,063 writing the card); #32 13,616
words written by guest compare-and-swaps (21.9 million of 150 million CAS on words one thread only writes); #57 / #34
single-object waits 1.6 million, 51,824 polls (33,703 timed out).
