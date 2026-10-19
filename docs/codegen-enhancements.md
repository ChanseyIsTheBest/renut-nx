# Codegen enhancements

The game code is C++ that the ReXGlue generator writes from the Xbox 360 executable (`app/generated`, 213 files). How
fast it runs on the A57 depends on how that C++ is written, so most of the CPU work on this port is done at build
time, in three layers:

1. **Generator options** in `app/renut_config.toml`, read by the SDK's code generator (`banjo-nx/sdk`);
2. **Post-codegen steps**: tools that rewrite the generated files, run by `tools/post_codegen.py` after every codegen
   (`app/CMakeLists.txt` appends it to the generator's command, because the PC build re-runs the generator by itself
   when an input changes);
3. **Compiler flags and build options** in `app/CMakeLists.txt`.

The guest-visible result is the same; only the shape of the C++ changes. Banjo ships **one NRO** (the user's rule),
built with all of it, so an A/B means two builds: most post-codegen steps have a CMake option whose OFF compiles their
statements as they were before the step (the generated file keeps both forms behind a macro); steps 1, 3 and 5 have
none (remove the step and rebuild); every generator option is a line in `renut_config.toml`. Two steps also have
run-time keys: the compare-and-swap barrier (`banjo_cpu_guest_cas_no_barrier`) and the localized copies
(`banjo_cpu_native_*`). The run-time keys are on
`docs/enhancements-reference.md`; the history is in `docs/enhancements.md`.

`python tools/enhancement_keys.py --audit-codegen docs/codegen-enhancements.md` checks this page against the sources
(see "Audit").

## Generator options (`app/renut_config.toml`)

| Option | Value | What it changes | Why the result is the same |
|---|---|---|---|
| `cr_as_local` | `true` | the condition register fields cr0-cr7 as C++ locals | registers of one function; nothing outside reads them |
| `ctr_as_local` | `true` | the count register as a local | as above |
| `xer_as_local` | `true` | XER (carry, overflow) as a local | as above |
| `reserved_as_local` | `true` | the `lwarx` reservation as a local | a `stwcx.` is a compare-and-swap of that value |
| `skip_msr` | `true` | no `mfmsr` / `mtmsrd` fences, `ctx.msr` stores or global lock around guest atomic sequences | the compare-and-swap already gives the atomicity |
| `dform_disp_split` | `true` | D-form loads and stores as base + zero-extended register + displacement | differs only when register + displacement crosses 2^32, a guest fault either way |
| `fctiwz_inline` | `true` | `fctiwz` as `fcvtzs` and a NaN select | the same result for every input |
| `fcti_inline` | `true` | `fctid`, `fctidz`, `fctiw` through `rex::ppc` helpers (`frinti` + `fcvtzs`) | **a correction on the Switch**: simde's portable path rounded exact halves away from zero (0.5 -> 1) where the 360 rounds to even, at 425 `fctid` sites. Checked at every start by the forms self-check |
| `fp_compares_modeless` | `true` | `fcfid`, `fctiwz`, `fctidz`, `fsel` switch no flush mode; `fcmpu`, `fcmpo`, `frsp` through any-mode helpers in VMX mode | those instructions do not depend on the mode; the helpers give the IEEE bits in either mode (unit-tested) |
| `fp_mode_lookahead` | `true` | before a scalar float instruction switches the mode, the next instructions that need one are looked at; loads, stores, conversions and one or two arithmetic instructions in front of vector code compute in whichever mode is current | any-mode helpers (unit-tested) |
| `fp_mode_lookahead_past_branch` | `true` | the any-mode form also when nothing that needs a mode follows before the next branch | as above. A bet: Banjo's PC census 868,000 -> 540,000 switches a second for 3.8 million more helper tests (11.5 per switch saved; SA2 lost it at 235) |
| `fp_copy_pairs` | `true` | `lfs` then `stfs` of the same register writes the loaded bits without a mode where VMX mode is current | the bits are moved, not computed |
| `div_inline` | `true` | integer divisions through helpers: `sdiv` / `udiv` on the A57 | they define a zero divisor and INT_MIN / -1 as the guard did |
| `fp_moves_modeless` | `true` | float loads, stores and moves switch no flush mode; `lfs` / `stfs` convert with helpers exact in either mode | `msr fpcr` sites 40,120 -> 30,988 in the Switch ELF (-22.8 %); the native reverb's verify compares the mode bit-exactly |
| `non_volatile_as_local` | `true` | r14-r31, f14-f31, v14-v31 and v64-v127 as C++ locals: no `__savegprlr` / `__restgprlr` helpers (35,300 static calls before) and no guest-stack traffic for them | audited for Banjo: setjmp copies the locals into the buffer and back (10 guest functions call it); the registers the game uses outside the ABI stay in ctx (next row) |
| `elide_lr_stores` | `true` | no `ctx.lr = <return address>` before every call | `ctx.lr` is read only by diagnostics; the guest unwinder is not implemented |
| `non_volatile_as_local_keep` | `["v64-v77", "v92"]` | those registers stay in ctx in every function | VMX128 constants built once by `sub_8239CE88` and read by about 900 functions, the WMV decoder's permute masks in v64 / v65 (every FMV was garbage without this), and an implicit argument in v92 |
| `non_argument_as_local` | `true` (2026-10-15) | r0, r2, r11, r12, f0 and v32-v63 as C++ locals: no store to ctx before and no reload after every call (`ctx.r11` uses 834,323 -> 12,747) | exact where no value crosses a call in them: `tools/non_argument_audit.py` finds every place one does (below); from sa2-nx via peggle2-nx |
| `dcbt_prefetch` | `true` (2026-10-16) | the game's 1,785 `dcbt` / `dcbtst` hints as the host's prefetch of the same address (`REX_PREFETCH`) | a prefetch never faults and changes no value; from marathon-nx (its 276 always on). Not measured alone: the A57's hardware prefetcher may already cover the sequential ones. CMake `BANJO_GEN_DCBT_PREFETCH=OFF` compiles them to nothing. **Fixed 2026-10-16** (CPU codegen agent): `emit_prefetch` read the operands as {RA, RB}, but dcbt decodes as {CT, RA, RB}, so every prefetch went to RA alone (or r0, often the null page) instead of RA + RB: 1.85 % of the console's game-thread samples were on those `prfm` |
| `rotate_word_shifts` | `true` (2026-10-16) | `rlwinm` / `rlwimi` with an immediate shift and a mask that does not wrap as the shift, extract or 32-bit rotate the mask makes of them: `(x << sh) & m`, `(x >> (32 - sh)) & m` or `rotl32(x, sh) & m` (`lsl` / `lsr` / `ubfiz` / `ubfx`: one instruction where the 64-bit rotate of the duplicated word took two or three). `rlwnm` (a register shift) unchanged | the same zero-extended value for every shift and mask: `sdk/tests/rotate_word_test.cpp` (5.1 million comparisons with the old form, both instructions). Measured by the CPU codegen agent on 12 files: -0.59 % instructions, -0.54 % weighted by the console's samples (the bytecode interpreter -2.0 %). Unlike `rotate_word_narrow` (rotate and mask, no gain), these are the forms GCC turns into one instruction |
| `non_argument_as_local_keep` | 1,225 functions | those functions keep the six registers in ctx; their callers hand the localized ones over (`emit_call_handing_non_arguments`) | the audit's ENTRY functions, the two EH dispatchers that pass r12 through an indirect call, and `sub_82381198` (see "Non-argument registers as locals") |

Measured for the "diet" (`non_volatile_as_local` + `elide_lr_stores`): Switch `.text` 64.71 -> 61.42 MB, NRO 66.1 ->
62.8 MB; masseffect-nx measured -10 % on its main thread on the console.

Not set, with the reason: `args_in_registers` (measured on 2026-10-16 and a net loss here: see "Measured and not
taken" below),
`rotate_word_narrow` (no gain with the Switch compiler, sa2-nx), `fp_copy_only_functions` (lost in sa2-nx's census).

## Post-codegen steps (`tools/post_codegen.py`)

Run in this order after every codegen. Every step is idempotent (a second run of the sequence changes no file) and
writes only the files that change, so a build does not recompile the others. ro_constants, the entry modes of
flush_mode_flow, leaf_dead_stores (which functions have their address taken) and dead_ctx_defs need the generator's
image dump, `out/renut_image.bin` (game data: never committed or distributed; `tools/pc_env.bat` makes the generator
write it).

| # | Tool | What it does | On Banjo's tree | Switch back (CMake) |
|---|---|---|---|---|
| 1 | `tools/pch_no_volatile.py` | guest loads and stores without `volatile` | masseffect-nx measured +15-20 % fps on the console | one NRO: remove and rebuild |
| 2 | `tools/indirect_calls.py` | tags every `bctrl` with its site; the sites of `app/indirect_promotions.txt` become guarded direct calls | 15,766 sites; 187 promoted (57.9 % of the census's calls) | `BANJO_GEN_PROMOTED_CALLS` |
| 3 | `tools/loop_barriers.py` | a compiler barrier at a loop header where a guest load could be hoisted: **required** with step 1, or GCC turns guest polling loops into endless loops. Since 2026-10-16 it clobbers the 4 GB guest window only, not all memory (marathon-nx's `PPC_LOOP_BARRIER`), so ctx registers and the host stack keep their host registers across it | 29,088 barriers; 7,418 of 36,506 loop headers need none | the barrier itself none (required); its narrow form `BANJO_GEN_NARROW_LOOP_BARRIER` |
| 4 | `tools/pch_cold_traps.py` | the trap helper out of line and cold | a layout change | `BANJO_GEN_COLD_TRAPS` |
| 5 | `tools/direct_calls.py` | calls between unhooked functions become direct calls (GCC can inline them); `--redo` sends the callers of newly hooked functions through their hooks again. Only the code of C/C++ files names hooks (from peggle2-nx, 2026-10-15: comments and `__imp__sub_X` calls of originals no longer keep 208 calls to 28 functions weak) | 125,517 calls; 465 hooked addresses kept | one NRO: remove and rebuild |
| 6 | `tools/guest_cas.py` | compare-and-swap without the legacy builtin's trailing `dmb ish` | 559 sites | `BANJO_SWITCH_GUEST_CAS`; run time `banjo_cpu_guest_cas_no_barrier` |
| 7 | `tools/flush_mode_flow.py` | flush-mode checks the control flow and the callees fix compile to nothing on the Switch | 8,819 of 21,771 checks proven | `BANJO_SWITCH_FLUSH_FLOW` |
| 8 | `tools/vec_byteswap.py` | guest vector byte reversal as `REV64` + `EXT` instead of a table lookup | 52,031 reversals | `BANJO_SWITCH_VEC_BYTESWAP` |
| 9 | `tools/pch_neon_shuffles.py` | constant word shuffles as one permutation | 9,101 table shuffles | `BANJO_SWITCH_NEON_SHUFFLES` |
| 10 | `tools/ro_constants.py` | loads from the image's read-only data as constants, except the XEX's import records (`--xex`: kernel-variable addresses the loader writes at run time; from peggle2-nx, 2026-10-15) | 18,079 loads (float, double, integer, vector); 77 loads of 12 import records put back as loads | `BANJO_GEN_RO_CONSTANTS` |
| 11 | `tools/float_loads.py` | a single-precision load straight into a float register | 24,329 loads (1,056 kept: a float was just stored to the address) | `BANJO_GEN_FLOAT_LOADS` |
| 12 | `tools/frame_traffic.py` | marks the prologue statements only; its three elisions stay off (**not exact**: sa2-nx found a game that reads a stack word it never wrote) | marks only | `BANJO_GEN_LR_MOVES_ELIDE`, `BANJO_GEN_NV_SAVES_ELIDE`, `BANJO_GEN_BACK_CHAIN_ELIDE` (all OFF) |
| 13 | `tools/loop_locals.py` | the ctx registers of a call-free loop as locals for the length of the loop; not in the 207 functions of `app/config/locals_optout.txt` that compile better without (`tools/locals_choice.py`) | 9,620 loop regions in 4,460 functions (2026-10-16, after the opt-out) | `BANJO_GEN_LOOP_LOCALS` |
| 14 | `tools/leaf_locals.py` | the ctx registers of leaf functions with loops as locals; not in the list's 89 leaf functions | 1,317 functions (1,406 before the opt-out) | `BANJO_GEN_LEAF_LOCALS` |
| 15 | `tools/vperm_one_source.py` | a `vperm` of one vector as a one-register lookup | 282 (1,863 two-source left) | `BANJO_GEN_VPERM1` |
| 16 | `tools/fp_sign_ops.py` | `fneg`, `fabs`, `fnabs` as float operations | 2,693 | `BANJO_GEN_FP_SIGN_OPS` |
| 17 | `tools/leaf_dead_stores.py` | a leaf's write-back of a register no caller can read is skipped | 5,084 of 27,974 write-backs, in 722 functions | `BANJO_GEN_LEAF_DEAD` (test: `BANJO_GEN_LEAF_DEAD_POISON`) |
| 18 | `tools/single_floats.py` | single-precision arithmetic computed in `float` where its inputs are proven singles | 16,279 of 27,756 instructions | `BANJO_GEN_SINGLE_FLOATS` |
| 19 | `tools/inline_memcalls.py` | `memcpy` / `memset` of a constant size up to 64 bytes inline | 1,269 calls | `BANJO_GEN_INLINE_MEMCALLS` |
| 20 | `tools/vec_unaligned_stores.py` | `stvlx` / `stvrx` as a few stores instead of byte loops | 1,012 | `BANJO_GEN_VEC_UNALIGNED` |
| 21 | `tools/ro_constants_cfg.py` | (2026-10-16, CPU codegen agent) the read-only constants step 10 could not see because a label stood between the base register's constant and the load (loop heads): a forward constant propagation over the function's control-flow graph, on `leaf_dead_stores.py`'s parser (a call forgets r0-r13 and the registers kept in ctx; an unknown instruction, indirect call, trap or mid-asm hook forgets everything). Each fold keeps the original load: `REX_RO_CFG_<kind>(address, value, load)` is the value on the Switch, the load compared with the value on the PC (address and bytes checked), the load with the switch off | 2,730 loads (2,665 `lfs`) on the tree of 2026-10-16; the agent's estimate, looser, -0.73 % weighted by the console's samples | `BANJO_GEN_RO_CFG` |
| 22 | `tools/dead_ctx_defs.py` | guest register writes no instruction can read are left out (since the eighteenth package; see below) | 18,754 plain writes and 15,340 record forms, in 13,846 functions | `BANJO_GEN_DEAD_DEFS` (test: `BANJO_GEN_DEAD_DEFS_POISON`) |
| 23 | `tools/flush_hoist.py` | (2026-10-16, the CPU codegen agent's aggressive round; right after step 22) a loop header's flush-mode check moved in front of the loop when the loop is call-free, its checks go one way only, the check is in the header's straight line with no float statement before it, and the only entries are the fall into the header or `loop_locals` stubs: every later iteration starts in that mode, and every exit has the same mode. On the PC it becomes `REX_FLUSH_KNOWN_*`, so the flush-flow check counts it | 178 loops in 99 functions; about -0.05 % (it reaches the inner loop of the copy `n_82B0DCB8`) | `BANJO_GEN_FLUSH_HOIST` |
| 24 | `tools/localize_native.py` | the localized native copies of `app/src/cpu/hot/localized.txt`, from the text as it stands after every step above | 25 copies (the XAudio limiter `sub_82B0D0C0` added on 2026-10-16) | run-time keys `banjo_cpu_native_*` |
| 25 | `tools/limiter_lazy_log10.py` | (right after step 24) the XAudio limiter's localized copy `f_sub_82B0D0C0` computes `log10(env)` only where the dB value is read (the `env > T` path, almost never taken: pow had 1 sample in 13,483 on the console). The skipped calls leave state behind that is reproduced before every real call and at the exit: only the last skipped call of each of `rex_log`'s four paths, in the order they happened (each path writes a fixed set of registers and stack words, the main path's set holds the other three; model-checked on 200,000 random sequences). The recompiled original is untouched: the hook's guard compares the copy with it. **Fixed the same day:** the first version replayed the skipped calls at the exit after the epilogue had given r1 back (`l_r1 += 192`), so their frames were built 192 bytes higher, in the caller's frame; the guard's first compare caught the stack word and switched the copy off. The replay is now placed before the epilogue: 23,205 calls compared, 0 differences | the copy was 6.3 % of the guest threads' samples (88 % of it in log10 / log): about -5 % of guest-thread time, half a core of the audio worker | `BANJO_HOT_LAZY_LOG10` (or the key `banjo_cpu_native_sub_82B0D0C0`) |
| 26 | `tools/switch_goto.py` | (`--thread --only <the five>`) the jump-table `switch` of the five script interpreters (`sub_82948928`, `sub_8294EF68`, `sub_82949790`, `sub_82BDAED0`, `sub_82959230`; about 5.6 % of the samples) as a computed goto with a copy of the dispatch at each handler's end; GCC's cross-jumping, which merges the copies back, is off for these functions only (`no-crossjumping`; `-ffp-contract=off` survives it). The guest's own bound check stays | 25, 19, 20 and 26 indirect branches where there was one; code +4 to +38 %; branch prediction only: estimated 0.2-0.5 % | `BANJO_GEN_SWITCH_THREAD` (`REX_SWITCH_GOTO_OFF` for the plain switch) |
| 27 | `tools/lr_preserve.py` | (`--assume-indirect --rewrite`) since `elide_lr_stores` ctx.lr changes only at the epilogues' write-backs, so a write-back stores the value ctx.lr already holds wherever every callee preserves ctx.lr (indirect calls assumed to): those write-backs compile to nothing (`REX_LR_WRITE_SAME`), at the sites of `app/config/lr_write_same_choice.json` | 12,369 of 14,561 sites proven; -0.18 % with the choice list | `BANJO_GEN_LR_WRITE_SAME` |
| 28 | `tools/r1_preserve.py` | the whole-program proof for step 29: a data flow over each function's control-flow graph (frame pointers, the back chain, epilogue fragments with a net delta, no-return calls) to a greatest fixed point, run twice: strictly, and assuming that indirect, hooked and import calls preserve r1 (`out/cpu_aggressive_proofs`, derived from the game) | 38,414 of 39,674 functions return with r1 unchanged (14,104 strictly) | (with step 29) |
| 29 | `tools/r1_local.py` | guest r1 as a local mirror (`r1l`) in the 10,731 functions of `app/config/r1_local_choice.json` (where it compiles cheaper; everywhere it is a loss, +0.33 %): every write of r1 is stored to ctx at once, the reload after a call goes, on step 28's proof | -0.41 % of guest time | `BANJO_GEN_R1_LOCAL` (`REX_R1_STRICT` reloads after assumed calls) |
| 30 | `tools/vsel_neon.py` | (package 27) VMX `vsel` (`(A & ~C) \| (B & C)`, written through simde's SSE2 forms, which GCC fuses into one `BSL` only some of the time: 2 of 6 in the culling routine `sub_829A6DE8`) as `REX_VSEL(C, A, B)`, NEON's `vbslq_u8` on AArch64: one instruction, the same bits | 2,813 sites | `BANJO_GEN_VSEL_NEON` |
| 31 | `tools/hot_layout.py` | (package 27) a `__attribute__((hot))` declaration in front of each function the console profile samples (`app/config/hot_functions.txt`, from package 21b's 8,323 samples of the game threads), so GCC puts it in `.text.hot.<name>` and devkitA64's `switch.ld` gathers them ahead of the rest of the 60 MB of `.text` (the A57's 48-entry first-level instruction TLB, its 48 KB instruction cache). In package 27's ELF they are one run of 2.27 MB; the 1,016 of them named `sub_*` touched 1,449 4 KB pages, 393 64 KB regions and 15 2 MB regions in package 26's ELF, and 584, 37 and 2 now. Only hot: nothing is marked cold (cold also means optimised for size). Placement, not behaviour | 1,273 of 1,273 listed functions | `BANJO_GEN_HOT_LAYOUT` |
| 32 | `tools/inline_leaves.py` | (package 27, last of all) each generated file gets a `static inline` copy of every tiny leaf function (at most 12 guest instructions, calling nothing, no static storage) it calls directly in another file, and calls the copy (`REX_INL`): GCC can inline it and keep the arguments in registers instead of handing them over through ctx. Only `__imp__sub_X(ctx, base)` calls of unhooked functions (step 5's); the same statements on the same ctx and memory | 9,909 calls in other files to 3,896 inline copies of 3,522 leaves | `BANJO_GEN_INLINE_LEAVES` |
| 33 | (in `post_codegen.py`) | `app/generated/codegen_flavor.txt`, the build's name in the `[enhancements]` line; written only when it changes (otherwise every build re-ran CMake and the codegen) | | |
| | (in `post_codegen.py`, `keep_unchanged_times`) | (2026-10-16) a generated file whose text is the last run's gets its old time back (never older than the generator's inputs), and the manifest, which the generator writes again on every run, a time no newer than its outputs: a CMake re-run (any new file) no longer recompiles the 213 files (about 20 minutes on the PC), and the build no longer runs the generator every time (`app/generated/codegen.times.json`) | | |

Origins: steps 1, 3 and 5 are the optimised codegen of 2026-10-07 (masseffect-nx's, through sa2-nx; step 3 narrowed
in sa2-nx's CPU rounds); 2, 4 and 6-14 are sa2-nx's three CPU rounds of 2026-10-12 (6-9 first from sasrt-nx); 15-20
are sasrt-nx's codegen round of 2026-10-14 (each with a unit test in `sdk/tests` except leaf_dead_stores, which has
the poison build); 21 is Banjo's own (2026-10-15);
22 is sa2-nx's mechanism with Banjo's guard. The counts are from the build logs of 2026-10-14 and 2026-10-15, except
steps 5 and 9 (2026-10-07 and 2026-10-13); each step's summary line in the build log gives the current ones.

Step notes:

- **fp_sign_ops** is exact while the game never selects rounding toward + or - infinity:
  `banjo_diag_runtime_census` counted 24 guest FPSCR writes to nearest, 10 toward zero and none up or down in 345 s to
  the level.
- **loop_locals** leaves alone any loop that calls a name it does not know (Banjo's mid-asm hooks), and both locals
  tools leave alone functions with setjmp / longjmp. It writes border stubs (`REX_LOOP_IN` / `REX_LOOP_OUT` copies
  under labels of their own) where a loop is entered or left.
- **leaf_dead_stores**, **single_floats** and **dead_ctx_defs** share one analysis of the guest program
  (`leaf_dead_stores.parse_function` and the call graph); see "The shared analysis" below.

## Compiler flags and build options (`app/CMakeLists.txt`)

| Option | Default | What it does |
|---|---|---|
| `BANJO_GENERATED_MATH_FLAGS` | `ON` | `-fno-math-errno` on the generated game code (sqrt is one instruction); IEEE results unchanged |
| `BANJO_GENERATED_NO_TRAPPING_MATH` | `OFF` | `-fno-trapping-math`, dropped on 2026-10-12: it lets GCC move a float operation across an `msr fpcr` mode switch. Packages up to 2026-10-07 had it |
| `BANJO_SWITCH_EARLY_RA` | `OFF` | OFF compiles everything with `-mearly-ra=none`: devkitA64 GCC 16.1's early register allocation dropped table lookups in Peggle 2's console build |
| `BANJO_SWITCH_OMIT_FRAME_POINTER` | `ON` | game code without the x29 frame chain (27,354 game functions set one up before) |
| `BANJO_SWITCH_NEON_VMX_HELPERS` | `ON` | `vperm`, `vctsxs`, `vctuxs` and ten small VMX helpers as NEON instructions |
| `BANJO_SWITCH_RENDERER_LINES` | `OFF` | line tables for the three hot renderer files (`tools/switch_hot_lines.py`); the same code |
| `BANJO_GEN_NARROW_LOOP_BARRIER` | `ON` | loop barriers clobber the guest window only (OFF: `REX_LOOP_BARRIER_FULL`, the `"memory"` clobber) |
| `BANJO_GEN_DCBT_PREFETCH` | `ON` | the generator's prefetches of the `dcbt` hints (OFF: `REX_PREFETCH` expands to nothing) |
| `BANJO_GEN_VECTOR_LOCALS_BY_VALUE` | `ON` | (2026-10-16, CPU codegen agent) the vector registers of the loop and leaf locals as values instead of references to ctx (`REX_VECTOR_LOCALS_BY_VALUE`), so a vector result is not stored to ctx on every iteration. Exact by construction (the same C++ semantics; peggle2-nx's fault of 2026-10-13 was `-mearly-ra`, off here). On all 213 files: -1.42 % weighted by the console's samples, +0.05 % instructions; 294 of the 481 functions it touches cheaper, 54 dearer; the XAudio mixers -15 to -19 % |
| `BANJO_GEN_RO_CFG` | `ON` | step 21 (`tools/ro_constants_cfg.py`); OFF: `REX_RO_CFG_OFF`, every such load as it was |
| `BANJO_GEN_FLUSH_HOIST` | `ON` | step 23; OFF: `REX_FLUSH_HOIST_OFF`, the check in the loop again |
| `BANJO_HOT_LAZY_LOG10` | `ON` | step 25; OFF: `BANJO_LAZY_LOG10_ON=0`, the copy as before |
| `BANJO_GEN_SWITCH_THREAD` | `ON` | step 26; OFF: `REX_SWITCH_THREAD_OFF`, one computed goto without the copies |
| `BANJO_GEN_LR_WRITE_SAME` | `ON` | step 27; OFF: `REX_LR_WRITE_SAME_OFF`, every write-back made. The PC build checks each (`REX_LR_WRITE_SAME_CHECK`, the `[verify]` line) |
| `BANJO_RENUT_RENDERER` | `ON` | (package 26) Renut's Direct3D-level renderer built into the same executable, chosen at run time with `banjo_renderer = "renut"` (`docs/renut-renderer-port.md`); not codegen, listed because it is a build option of `app/CMakeLists.txt` |
| `BANJO_SWITCH_OUTLINE_ATOMICS` | `OFF` | (package 27) OFF compiles the Switch build with `-mno-outline-atomics`, so no atomic read-modify-write becomes a call to a libgcc helper (`__aarch64_cas4_acq` and the like, which test for LSE). Measured: no effect, devkitA64's GCC does not outline them for this target; the 75 helper calls in the ELF are in the prebuilt NVK's Rust code, before and after |
| `BANJO_GEN_VSEL_NEON` | `ON` | step 30; OFF: `REX_VSEL_OFF`, simde's expression |
| `BANJO_GEN_HOT_LAYOUT` | `ON` | step 31; OFF: `REX_HOT_LAYOUT_OFF`, the declarations empty |
| `BANJO_GEN_INLINE_LEAVES` | `ON` | step 32; OFF: `REX_INLINE_LEAVES_OFF`, every call to the original |
| `BANJO_GEN_R1_LOCAL` | `ON` | step 29; OFF: `REX_R1_LOCAL_OFF`, the mirror a reference to ctx.r1 (the old code). The PC build compares the mirror with ctx.r1 after every kept or assumed call (`REX_R1_CHECK`) |

Always on, not switchable: `-ffp-contract=off -fno-strict-aliasing` for the app and the game code. Without the first,
GCC fused separate guest multiplies and adds into FMAs, so the console's floating point differed from the 360's and
the PC build's.

**LTO and PGO** are left for the release build (the user's decision); `app/src/banjo_pgo.cpp` is written and not
compiled in.

## The localized copies and the hand-written natives

Generated at build time by step 22 and switched at run time; documented with their keys on
`docs/enhancements-reference.md` ("Game code as native code"), and the ranking in `docs/enhancements.md`. What they
say about the codegen, for every port:

- **A copy wins where it swallows calls.** Banjo's build already keeps registers in host registers where it can
  (non-volatile locals, loop and leaf locals, `__restrict` ctx), so a copy of a function that calls nothing is not
  shorter: the hook's 14 instructions and the loads and stores at entry and exit cost what the locals save (40 of 59
  candidates). `sub_823A74E8` (one draw's state set-up) goes from 16.6 calls a call to 1.3 and from 1,075 to 794 A57
  instructions.
- **The four large traversal functions get longer as copies** (+19 % to +54 %). In the three with about forty live
  context registers the locals do not fit the A57's registers and become stack slots; why the fourth, the bytecode
  interpreter `sub_82948928`, grows was not looked into.
- **The call hand-over is the cost that is left.** No guest function is inlined into another on the Switch build:
  every call stores its arguments to ctx, sets up a frame and loads everything back. Flattening the call chain inside
  the tool (one set of locals for a caller and its callees) is the next idea, not attempted.
- **Hand-written natives copy in guest byte order.** The generated constant writers and state-flush writers swapped
  every word twice (guest to host and back); the natives move the bytes as they are: -25 % to -85 % A57 instructions
  per call.

## The shared analysis, and its fixes of 2026-10-15

`tools/leaf_dead_stores.py` reads the guest program from the generated text: per function its instructions and jumps
(`parse_function`), and over the call graph what each function's callees read before writing (UE) and what its
callers read after it returns (LiveOut), to fixed points. Nothing is assumed from the calling convention between
direct calls (the 360 compiler allocated registers across them). `single_floats.py` and `dead_ctx_defs.py` use the
same parser and graph. Building `dead_ctx_defs.py` found four faults in it:

1. **Border stubs** (all three tools). A `goto` to one of `loop_locals.py`'s border stubs was taken for an edge to the
   instruction after the stubs (often the loop header: a different place), so a path through the stub was missing.
   `parse_function` now resolves every goto to a stub to the stub's own target (`resolve_stubs`). After the fix
   single_floats proves 31 instructions fewer (16,310 -> 16,279). sasrt-nx, which has the same tools, confirmed that
   the fix changes its build too. **The NRO of the seventeenth package (2026-10-15 02:45) and the sixteenth were built
   before this fix** (and before fix 4). The eighteenth package has both.
2. **Functions the generator names** (`rex_*`, the C runtime and Microsoft's libraries: hundreds). They counted as
   reading every register. They are now read from the generator's function table and their calls resolved like any
   other, so the analysis sees through them: leaf_dead_stores skips 5,084 write-backs where it skipped 3,195.
   Before this result ships, the poison build (`BANJO_GEN_LEAF_DEAD_POISON`) has to run the route again.
3. **Record forms** (dead_ctx_defs only). `rlwinm.` and friends set cr0 from their result; removing the instruction
   that fed one (an `lbz` into a scratch register) broke the bit test. A record form now keeps its reads.
4. **Mid-asm hooks** (all three tools; the crash that kept dead_ctx_defs out of the build). The generator calls a
   hook's host function in the middle of the guest code: with the registers it names passed by reference
   (`textureTools_SetTexture_hook(ctx.r5);`), or as a test that may jump
   (`if (disable_particles_draw0()) { goto loc_X; } else { }` followed by the guest call). The parser takes a guest
   instruction's registers from its comment, so it saw neither the registers a hook reads nor that the hook's `goto`
   is conditional: it took the goto for the end of the instruction and never saw the call after it. In
   `sub_8222ADA0` (the particle draw) the `this` pointer in r3 and the argument r4 = 1 of the four calls behind
   `disable_particles_draw0`-`3` looked dead and were left out, and the callee wrote through a garbage pointer
   (guest address 4). Now a hook's arguments are reads and its test's gotos and returns are branches it may take;
   the 40 hooks are the generator's `extern` declarations (`leaf_dead_stores.load_mid_asm_hooks`). Found by
   bisecting the rewrites file by file (`tools/dead_defs_bisect.py`: 213 files -> renut_recomp.120.cpp) and comparing
   the analysis with and without the fix. The fix changes no leaf_dead_stores result and one single_floats
   instruction (an `fmuls` 7,201 -> 7,200 proven single): the packages up to the seventeenth have that one wrong.
   Every other host call in the generated code passes `(ctx, base)` and was already taken to read everything (a
   survey of every call form in the 213 files).

### `tools/dead_ctx_defs.py` (on since the eighteenth package)

In a function that calls anything, the volatile registers live in the context, and every guest instruction that sets
one stores it there; the compiler keeps the store because a call or a guest load might read ctx. A write that the
guest program never reads still costs its store and its computation. The interpreter's dispatch, for example, loads
the jump table entry into r0 and the table address into r12 for a `mtctr; bctr` that the generated `switch` no longer
needs. The tool does a backward liveness per function on the shared analysis, removes writes that are live nowhere,
then their inputs, to a fixed point: `REX_DEAD_DEF(reg, statement)` (nothing), or `REX_DEAD_REC(...)` for a record
form (the value computed into a temporary for cr0). With `--abi-indirect` a function called through a pointer is read
by its caller only in the calling convention's terms.

On Banjo's tree: 18,754 plain writes and 15,340 record forms left out, in 13,846 functions.

**History.** Its first PC builds crashed at start. The record-form and border-stub faults (above) were two causes.
The last was the mid-asm hooks (fault 4): bisected by file with `tools/dead_defs_bisect.py` (`save`, `set <files>`,
then a build with `BANJO_DEAD_DEFS_HOLD=1`; 213 files -> 54 -> 27 -> 13 -> 6 -> 3 -> `renut_recomp.120.cpp`), then
read. The PC crash handler now also logs the module and offset of the faulting host instruction
(`sdk/src/system/mmio_handler.cpp`), for `llvm-symbolizer`.

**Checked (PC, the console's `renut.toml`, intro route of 300 s: the movies, title, menus, the intro cutscenes,
Spiral Mountain):** the normal build and the poison build (`BANJO_GEN_DEAD_DEFS_POISON=ON`: every left-out register
written 0xDEADBEEF...) both ran the route with no crash and 0 `DIFFERENCE`, all 24 localized copies matching their
originals, and recordings without anything broken. What the route does not reach (other levels, vehicles, the
garage) has not been run with it; the console run is its test.

**Switches.** `BANJO_DEAD_DEFS_OFF=1` in the environment of a build takes the rewrites back out
(`post_codegen.py` runs the tool with `--restore-only`); `-DBANJO_GEN_DEAD_DEFS=OFF` compiles the statements again with
the rewrites in place (`REX_DEAD_DEFS_KEEP`); `BANJO_DEAD_DEFS_NO_ABI=1` runs it without `--abi-indirect`.

## Non-argument registers as locals (`non_argument_as_local`, 2026-10-15)

From sa2-nx via peggle2-nx (its report `D:\peggle 2\peggle2-nx\out\reports\codegen_proposals_for_banjo.md`, proposal
1). MSVC uses r11, r12 and f0 as scratch everywhere and r0 for jump tables; in ctx each one is stored before and
reloaded after every call. As C++ locals GCC keeps them in host registers. The registers are volatile in the Xbox 360
ABI and pass no arguments, so a value crosses a call in them only in five ways, and the audit finds each:

1. **A callee reads one on entry** (an argument outside the ABI). `tools/non_argument_audit.py` on code generated
   *without* the option (`rexglue codegen --ignore-stamp` into the tree, copied before `post_codegen.py`), with the
   no-return functions `--noreturn=82BB59B0,822B27A0,822B8648,82BAEAB8,82BB1130,82BB4AE0,82BB4D00,82BB9B78,82BCDC10,
   rex_longjmp,rex_exit,rex_j_std___Throw,rex_std___Throw,rex_abort,rex_amsg_exit,rex_exit_1,rex_ExitThread`:
   1,441 ENTRY lines. Apart from the `__save*` / `__rest*` helpers (kept by name), 1,223 functions: the MSVC unwind
   funclets (`addi r31,r12,-N`, the frame in r12), the stack probe, the VMX top save and restore (v32), and about 200
   r11 readers with no caller (start points the function scan found in data). All on the keep list.
2. **A value passes through an indirect call**: there is no hand-over there. Only `rex_CallSettingFrame` and
   `rex_C_ExecuteTerminationHandler` set r12 (the frame) from an argument before an indirect call (to a funclet): kept.
   (The guest unwinder that would run them is not implemented, so the funclets never run here; kept anyway.)
3. **A callee returns a value in one** (RET, 4 after the audit's longjmp fix):
   - `sub_82BCA7B8`, `rex_lock_fhandle`'s `__finally`, returns r11: the share_registers copy-back takes it;
   - `sub_82381198`: its jump table entry 21 lands after its stub's `lis r11` (the byte table's 8-byte part), so it
     uses its caller's r11: kept, and its callers hand r11 over;
   - `sub_823854C8` reads r11 after an indirect call only in switch case 17, which the `r3 <= 14` test before the
     switch rules out.
4. **A read before a write whose value never matters** (shift registers, `subfe rX,r11,r11`): none reported for r0-r12
   or f0; the v32 ones are the kept VMX save and restore.
5. **Host code**: no hook, native or SDK file names these registers (`mullhwucrash.cpp`, which does, is not built).

**Generator changes** (`sdk/src/codegen`): sa2-nx's `non_argument_as_local_keep` (`config.cpp`), `keepsNonArgumentsInCtx`
and the hand-over at `bl`, `b`, `bctr` calls and **conditional tail branches** (`builders/context.cpp`,
`builders/control_flow.cpp`). The first port here missed the conditional tail branch (`if (cr6.eq) { sub_X(ctx, base);
return; }`, `emit_conditional_branch`; peggle2-nx's copy has it) and the audit caught it: `sub_82C83DD8` sets r11 and
branches to its split continuation `sub_82C83E5C`, which reads it.

**After generating with the option**: `non_argument_audit.py --with-option` reports 6 HANDOVER, 11 RET and 5 new ENTRY
lines, and `read_before_write.py --volatile` reports 5. All are the setjmp support of the generator: at a `ppc_setjmp`
it copies every C++ local into the buffer (`env.r11 = r11`, also when nothing has written r11 yet) and back after a
`longjmp`. The guest's `longjmp` leaves the volatile registers undefined, and with the option off the audit sees no
read of them in those five functions (`sub_8272A7D0`, `sub_8276A798`, `sub_8276AC08`, `sub_828E9410`, `sub_828E99E0`):
nothing reads the copied value.

**Tool changes** (`tools/non_argument_audit.py`, from peggle2-nx): `--noreturn` takes function names as well as
addresses; a function whose last statement is the direct `ppc_longjmp` does not fall off its end (it made 33 false RET
lines after an assert reporter, `sub_8281AF28`). `tools/vec_shiftout.py` is copied for future v32-v63 cases (none now).
`tools/direct_calls.py` does not count the keep list's addresses as hooks.

**Checked**: a poison PC build (the new locals start at 0xDEADBEEF...; `nonarg_poison.py`, not kept) on the intro
route (no crash, no difference, the same pictures); a verify run (0 differences). Measured on the Switch ELF, recompiled
game functions, nineteenth -> twentieth package (with `direct_calls` code-only and `ro_constants --xex`):
instructions 7,733,331 -> 7,388,065 (-4.5 %), stores 1,655,642 -> 1,467,663 (-11.4 %), loads -1.6 %, byte reverses
-6.9 %; NRO 63.18 -> 61.81 MB. Console time to come.

## marathon-nx's review (2026-10-16)

At this port's request the marathon-nx session compared its hand-written natives and codegen with this page and
proposed six items (its report and read-only scripts: `C:\devkitPro\MarathonRecomp-NX\out\reports\
codegen_proposals_for_banjo.md`, `banjo-audit-tools\`). Audited and decided here:

| # | Proposal | Decision |
|---|---|---|
| 2 | Narrow loop barrier: clobber the guest window only | **Done** (step 3). Exact: ctx is thread-private (a call already makes the compiler reload it), every guest load in the loop is still issued again; Marathon's floating-point exception came from its `-ffp-contract=fast`, which Banjo does not use. PC verify run: 0 differences |
| 4 | `dcbt_prefetch` | **Done** (generator option), with `BANJO_GEN_DCBT_PREFETCH` to switch it off |
| 1 | Write-through stack-slot forwarding (a reload of a stack slot after calls uses the stored value) | **Not done**. The proposal's reach rule missed two ways a callee writes into its caller's frame: frameless leaves (their r1 is the caller's: 476 functions write there) and a callee's callee through a small middle frame. With the reach computed over the call graph (unknown callees, indirect calls included, count as writing everything) 311 reloads in 54 functions remain of 24,398, none in a function with samples in the console profile of 2026-10-15 (`scratchpad forward_estimate.py`). Recovering more needs the address-taken set for indirect calls; not worth it at this size |
| 3 | Choose the locals form per function from the compiled code | **Done**: `tools/locals_choice.py` compiles every generated file the locals tools changed twice with the Switch build's command line (as built, and with `REX_LOOP_LOCALS_OFF` / `REX_LEAF_LOCALS_OFF`) and compares each function's machine code, loop-weighted (cycles of the control-flow graph; code that only runs after a trap left out). Of 6,073 functions, 4,925 are cheaper with locals and 296 without (207 loop, 89 leaf; more than 2 %), mostly functions with 30 or more locals that spill (25 of 47 lose). Those are in `app/config/locals_optout.txt`, which both tools skip; the static loop-weighted cost of the changed functions falls 1.8 %. A new list needs a fresh codegen (edit the comment in `renut_config.toml`) |
| 5 | `eieio` as `dmb ishst` | **Not done**: the 18 sites are memory-mapped register writes (`Atg_WriteMMRegister` and the like), not a hot path |
| 6 | Native zlib inflate | **Not needed**: Banjo's archives are XCompress (LZX): `rex_fast_decode_aligned_offset_block_XCOMPRESS` is the decoder in the console profile (0.7 % of the game threads' samples in a diagnostics run), not zlib's inflate |

The console profile used (diagnostics run of 2026-10-15): of the samples on the game's threads, the generated game
code is about a third, spread thin (the largest single function, `debugHookCall` with its inlined callee, 11 %); the
rest is audio (`banjo_reverb`, XAudio), the hand natives, `memcpy`, and the profiler overlay's ImGui drawing.

## The CPU codegen agent's review (2026-10-16)

An agent of this session profiled the console runs of package 20e (11,973 samples of the game threads, steady state),
sorted every A57 instruction of the 39,724 recompiled functions into classes, and compiled every proposal on copies
of the 213 generated files with the build's own command line (its report and scripts: `out/cpu_proposals/REPORT.md`;
everything in that folder is derived from the game). Decided here:

| # | Proposal | Decision |
|---|---|---|
| 1 | `dcbt` operands (a generator bug) | **Done** (`dcbt_prefetch` above) |
| 2 | Vector locals by value | **Done**: `BANJO_GEN_VECTOR_LOCALS_BY_VALUE` (the global define; a choice per function measured only 0.04 % more) |
| 3 | A localized copy of the XAudio limiter `sub_82B0D0C0` (`rex_log10` + `rex_log`: 7.3 % of the game threads' samples) | **Done**: a line in `localized.txt` (its closure: 11 functions, log10 / log / pow); its guard checks it |
| 4 | Read-only constants over the control-flow graph | **Done**: step 21 |
| 5 | `rlwinm` canonical forms | **Done**: `rotate_word_shifts` |
| 6 | Generated files as contiguous guest-address ranges (0.7 % of direct calls stay in their file today) | Not yet: needs a console A/B |
| 7 | An empty `REX_LR_WRITE` on the Switch | Not yet: needs a PC counter first |
| 8 | Direct calls of named functions | Not yet: no gain alone (with 6 or LTO) |

Also found: `townsfolk.cpp`'s "Disable Townsfolk" tests `ctx.lr`, which `elide_lr_stores` never sets, so it does
nothing; two threads spin at about 55 % each for the first 10 s in `sub_823DFE68` while a `debugHookCall` table
entry is still 0.

## The aggressive round (CPU codegen agent, 2026-10-16)

Asked for changes that may hold for this game alone. Measured like the first review (every file compiled with the
Switch build's command line and costed per function with the A57 model, weighted by the console's samples of package
21b, the first 30 s of each run left out); its tools and data are in `out/cpu_aggressive` (derived from the game, not
for distribution). Taken: steps 23 and 25-29 above. Not taken:

- **frame_traffic's three elisions on every file** (LR moves -0.32 %, back chain -0.31 %, NV zero saves -0.10 %): not
  proven; `frame_audit.py` found 20,544 frame loads of slots whose address escaped to a callee first (out-parameters,
  which need interprocedural must-write summaries). A poison build, as for `dead_ctx_defs`, is the route to that 0.7 %.
- **r1 mirror in every function**: +0.33 %, a loss (callee-saved spills); only the per-function choice pays.
- **No loop barriers at all**: +2.95 % (an upper bound, and a loss).
- **Lazy forms of other math**: `rex_exp`, `pow`, `floor` have 30-80 samples each.
- **Open**: the copy `n_82B0DCB8` (2.81 %) is bound by store-to-load forwarding of its filter state through guest
  memory (keeping it in registers behind an alias check: maybe 0.6 %); `sub_8264DCD0` (2.7 %) is Direct3D's GPU-wait
  spin, renderer work; flush-mode switches are 3.7 % of the recompiled code's samples, spread thin.

## Measured and not taken (2026-10-16)

Two larger ideas, measured before building them.

**Arguments in registers (the SDK's `args_in_registers`).** The generator's pass (`sdk/src/codegen/args_in_registers.cpp`,
from masseffect-nx; peggle2-nx keeps the reference implementation in its own tools folder) gives direct calls between ordinary
functions a fast entry that takes r3-r10 / f1-f13 / r0 / r11 / r12 / f0 / lr as C++ arguments and returns r3, behind a
`sub_X(ctx, base)` wrapper for indirect calls and hooks. On Banjo's executable it converts 37,620 of 39,674 functions:
136,425 direct call sites pass 415,767 arguments by value, 29,085 calls keep the old ABI. Two scratch codegens (the
same `renut_config.toml`, the pass off and on; hooked functions, memcpy and memset excluded), 11 of the 213 files
compiled with the Switch build's command line and compared function by function (`tools/locals_choice.py`'s cost
functions; loop-weighted cost as there):

| Guest memory | Instructions | Loads | Stores | Stack loads + stores | Loop-weighted cost |
|---|---|---|---|---|---|
| volatile (the generator's own pch) | +6.9 % | +11.6 % | -12.8 % | +40 % | -4.4 % |
| plain (`pch_no_volatile.py`, as the build ships) | +10.8 % | +21.3 % | -5.4 % | +30 % | **+2.8 %** |

With the memory model the build ships it is a loss, and removing the compiler barrier the pass puts at every fast entry
changes nothing (+2.78 %): the cost is structural. Values that crossed calls in ctx now cross them in callee-saved
registers and stack slots, every converted function with direct callers becomes a wrapper plus a fast body, and the
16,215 functions with no direct caller load their arguments at entry. The ctx traffic the pass was written against is
mostly gone already (`__restrict` ctx, `non_volatile_as_local`, `non_argument_as_local`, the loop and leaf locals:
those files' loop-weighted cost is 31.7 % below the raw generator output with the shipped pipeline). Its fast bodies
would also be invisible to every post-codegen tool that looks for `DEFINE_REX_FUNC`, `tools/loop_barriers.py` included,
which the plain guest memory needs. The call boundary that remains is LTO's job in the release build (it removed 24 %
of marathon-nx's guest calls). Two cautions for a port that tries it anyway: its assumption that indirect callees never
read r0 / r11 / r12 / f0 is false for 1,225 of Banjo's functions (`non_argument_as_local_keep`), and a statement that
passes `ctx` whole other than `name(ctx, base);` is not seen as a call.

**Lazy byte order** (leave a loaded value in guest byte order while it is only copied, tested or masked). In the Switch
ELF's game code, of 632,302 byte reverses: 43.1 % feed arithmetic or an address, 36.7 % are the swap before a store,
5.0 % feed only an AND / OR / EOR with a constant, 0.7 % only an equality test with zero, 10.6 % have their use after a
branch. Only 4,485 (0.7 %) reverse a value a reverse just produced: GCC already cancels the double swap of a copy inside
a function. 459,373 (75 %) follow a load. The removable part is the masks and equality tests, under 1 % of the game
code's instructions: not worth a transform.

## Package 27's codegen round (2026-10-17)

The user asked for 15 aggressive codegen ideas, audited, and the ones that hold implemented. Three are in the build
(ideas 2-4); the other twelve were measured or audited out, one of them (idea 1) because it turned out to change nothing. Each idea was checked for
exactness first, then measured: the generated files compiled with the Switch build's own command line and costed on the
A57 model (`out/cpu_proposals/cc.py`), or, for placement, counted in the ELF. The console is the judge; none of these
can show on the PC.

| # | Idea | Audit | Result |
|---|---|---|---|
| 1 | `-mno-outline-atomics` | exact: the inline loop is the one libgcc's helper runs on a CPU without LSE | **no effect**: devkitA64's GCC does not outline atomics for this target. Package 26's ELF has 75 helper calls, all in the Rust standard library inside the prebuilt NVK (NAK's compiler), none in banjo-nx's code; package 27's the same 75. The flag stays (`BANJO_SWITCH_OUTLINE_ATOMICS`, the same code) |
| 2 | VMX `vsel` as one NEON bit select | exact (bitwise); GCC fused only some of simde's three operations | **taken**, step 30 (2,813 sites) |
| 3 | the profile's functions gathered in `.text.hot` | placement only; GCC's `hot` also tunes those functions within -O3's rules (no fast-math, `-ffp-contract=off` stays) | **taken**, step 31 |
| 4 | tiny leaves inlined across files | exact (the same statements); hooks respected (only step 5's direct calls) | **taken**, step 32 |
| 5 | localized copy of `sub_823B59C0` (recursive routine, 0.93 % + 0.84 % of the game threads' console samples) | the copy mechanism and its guard: 4,198,210 calls compared in a verify run, 0 differences | **not taken**: dearer. `tools/hot_arm_compare.py` on the block counts of a PC route run (3.8 million calls): 708 A57 instructions a call -> 876 + 14 for the hook (+26 %; the host stack 39 -> 54: its 33 live registers spill) |
| 6 | localized copy of `sub_82951B78` (sixteen calls, 0.87 %) | the same, but the guard compared none of its 411,841 calls: every one reaches a call the copy keeps (30 left real) | **not taken**: 2,247 -> 2,055 + 14 A57 instructions a call (-7.9 %, about 0.07 % of the game threads) is not worth a copy its guard cannot check |
| 7 | jump-table defaults unreachable where the guest's own bound check dominates the `switch` (`tools/switch_unreachable.py`) | exact per site | **not taken**: GCC's value-range propagation already uses the guest's check: -4 instructions in 6 files |
| 8 | `-fvisibility=hidden` | exact | **not taken**: 0 instructions of difference in the generated code |
| 9 | `lvlx` + `lvrx` + `vor` as one unaligned load | exact only for the complete idiom | **not taken**: the pairs are not adjacent in the generated code (other statements between them), so the idiom cannot be proven from the text |
| 10 | more indirect-call promotions (step 2) | exact with the guard | **not taken**: the next sites are polymorphic; a compare chain mispredicts like the indirect branch, and the gain is below what can be measured |
| 11 | the write-backs of ctx.lr step 27 could not prove, empty | needs every one to be the value ctx.lr holds | **not taken**: not provable (the 2,191 sites step 27's analysis leaves) |
| 12 | step 12's frame elisions with a poison build | sa2-nx found a game reading a stack word it never wrote | **not taken**: exact only with must-write summaries of every callee, not written |
| 13 | a yield in the start-up spin of `sub_823DFE68` (two threads at 55 % for the first 10 s) | timing only | **not taken**: start-up only, and a native rewrite of the poll is a risk for no frame |
| 14 | `vmsum4fp128` (four-wide dot product) as NEON | must keep the 360's addition order | **not taken**: simde's order has to be matched bit for bit; not proven |
| 15 | the contiguous partition (callers and callees in one file) | exact | **not taken**: a generator change whose only effect is placement and inlining across files; it needs a console A/B (idea 4 takes the part that can be shown exact) |

`tools/hot_arm_compare.py` reads `.text.hot.<name>` sections too since this round (step 31 moves the hot functions
there; without it the originals measured empty).

Measured on the A57 model over the 213 files (ideas 2-4 together, static, every block once): -0.02 % weighted by the
console's samples, +0.72 % instructions in total (the inline copies count in every caller). What the model cannot see is
what they are for: the call and the hand-over through ctx that a GCC inline removes at run time (the model counts the
copy's body where the call was), and the instruction TLB and cache misses of 600 scattered pages. The console profile of
package 27 says whether they moved anything; each has its switch for an A/B build.

Also measured and not taken in this round: localized copies of the XAudio mixer, sample-rate converters and router
(they make no calls a copy could inline, and their loops already keep registers in locals: a copy only adds the hook).

## Checks of the generated code

- **The PC build compares.** Where a step has a run-time form, the PC build keeps the old operation next to the new
  one and compares on every execution: `[verify] flush-mode flow: N checks proven redundant executed so far, 0 had to
  switch the mode` and `[verify] generated-code self-checks so far: N loads folded to read-only constants, N promoted
  indirect calls, N critical section owners from the PCR; 0 differences`. Verify session of 2026-10-13: 730 million
  proven flush checks, 595 million folded constants, 54 million promoted calls, 18 million owners, none wrong.
- **Every start checks the forms one processor has** (`app/src/cpu/banjo_forms_selfcheck.cpp`): every helper with an
  AArch64-only or x86-only form against a plain C++ reference (248 results).
- **Unit tests**: `tools/run_sdk_tests.bat` (`sdk/tests`: the any-mode helpers, the conversions, vperm, memcalls,
  unaligned stores, the PMULL fingerprint, the signal skip).
- **Poison builds** for the steps that drop a write: `BANJO_GEN_LEAF_DEAD_POISON` and `BANJO_GEN_DEAD_DEFS_POISON`
  write 0xDEADBEEF... (a signalling NaN for floats) where the write was dropped, so a reader the analysis missed breaks
  the game visibly. Never for a build that ships.
- **ELF checks**: `tools/find_self_loops.py <elf>` must find no self-loop in a recompiled function except
  `sub_82C25230` (a ring-index walk on registers that spins on the 360 too); `tools/arm_cost_census.py` counts
  instructions, `msr fpcr`, `dmb`, frame set-ups per build; `tools/read_before_write.py` lists functions that read a
  register before writing it (the audit of `non_volatile_as_local`).
- **What has no run-time check**: the loop and leaf locals, the float loads and the compare-and-swap are exact by
  construction (sa2-nx's argument and its lockstep frame comparison). Banjo has no lockstep frame comparison.

## For the other ReXGlue ports on ARM

What this port's function-level work suggests for the shared generator and tools:

- **Dead context writes** (`dead_ctx_defs.py`, once its fault is found): a guest register write in a calling function
  is a store the compiler cannot drop. Bit tests into a scratch register (record forms) are the commonest case;
  indirect-jump dispatch (`mtctr` + `bctr` after a jump table) is one the generator could drop at the source: the
  generated `switch` reads the index register, never ctr.
- **Hashing on the A57**: CRC32 instructions all issue in one pipeline (4 bytes a cycle at best, 1.5 measured); a
  PMULL-folded CRC-64 has independent chains and does about twice the bytes a cycle with the same guarantees
  (`app/src/me/masseffect/banjo_pmull_fingerprint.h`, its test in `sdk/tests`).
- **Copies of the guest program for a hot path**: only where calls disappear; measure A57 instructions per call on
  real block counts (`tools/hot_arm_compare.py`), not on the PC.
- **Any tool that parses the generated text** after `loop_locals.py` must follow its border stubs.
- **`non_argument_as_local`'s hand-over** covers conditional tail branches too (`emit_conditional_branch`), not only
  `bl`, `b` and `bctr`: a port of the option that copies only the call paths misses it, and only
  `non_argument_audit.py --with-option` shows it.
- **`direct_calls.py`**: only the code of C/C++ files names hooks (comments and `__imp__sub_X` calls of originals do
  not: 128 of Banjo's 592 "hooked" names); and a keep list in a toml it reads whole is not a list of hooks.
- **`ro_constants.py --xex`**: the XEX's import records (kernel variable addresses the loader writes) are not
  constants; in these executables they start at the beginning of `.rdata` (futari-nx's finding).
- **`dcbt` in the generator** (2026-10-16): the instruction decodes as {CT, RA, RB}; a builder that reads operands 0
  and 1 prefetches RA alone (or r0). Every port whose generator came from the same `emit_prefetch` has it.
- **`rotate_word_shifts`**: the plain shift forms of `rlwinm` / `rlwimi` are what GCC makes one instruction of; the
  rotate-and-mask form (`rotate_word_narrow`) is not.
- **Read-only constants over the control-flow graph** (`ro_constants_cfg.py`): most float literals are loaded in
  loops through a base set before the loop; a straight-line pass misses them at the loop head.
- **The XAudio mastering limiter** (`limiter_lazy_log10.py`, 2026-10-16): the XDK links it statically; its per-sample
  log10 is read only when the envelope is over the threshold, which almost never happens. A port with it finds it by
  the log10 + pow pattern, localizes it, and applies the tool (it checks the generated statements first). When a
  skipped call's state is replayed, replay it before the epilogue gives the stack pointer back.
- **Threaded dispatch** (`switch_goto.py --thread`): GCC merges the dispatch copies back into one unless
  cross-jumping is off for those functions (`#pragma GCC optimize("no-crossjumping")`).
- **r1 as a local** (`r1_preserve.py`, `r1_local.py`): a loss on every function (callee-saved spills), a gain on a
  per-function choice from measured costs.
- **The build**: a generator that rewrites its outputs (and its manifest) with the same text on every run makes ninja
  recompile everything; give unchanged files their old times back (`post_codegen.py`, `keep_unchanged_times`).

## Audit

`python tools/enhancement_keys.py --audit-codegen docs/codegen-enhancements.md` checks: every option of the generator
table has the value `app/renut_config.toml` sets; every CMake option named exists in `app/CMakeLists.txt` and the
build-option table gives its default; every `tools/*.py` named exists; the post-codegen table lists the steps in the
order `tools/post_codegen.py` runs them.
