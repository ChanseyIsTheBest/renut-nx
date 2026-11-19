# banjo-nx on the ultimate base: notes

What the rebase met while building and testing banjo-nx (Banjo-Kazooie: Nuts & Bolts, title update 3, XDK 2.0.7978)
on renut_nx_ultimate_base 6cd02ae7, with its evidence. The inventory and the method are in
docs/ultimate-rebase-plan.md; the not-exact forms' censuses in docs/banjo-near-exact.md; the order of events in
docs/ultimate-progress-log.md.

## Setting up the worktree (2026-11-19)

A worktree has only the tracked files. What the builds also need, none of it committed:

| What | From | Why |
|---|---|---|
| `app/assets` | a junction to `D:\Banjo kazooie port\game` (default.xex with title update 3's default.xexp) | the manifest reads `assets/default.xex` |
| `app/generated/rexglue.cmake` | the base's working tree (the generator writes it; port 69's PCH byproduct in it) | the app's CMakeLists includes it before the first code generation |
| FFmpeg's configure output (`config.h`, `libavutil/avconfig.h`, `ffversion.h`, the `*_list.c`) | the base's working tree (the same as banjo-nx's) | ignored by FFmpeg's .gitignore; the SDK's FFmpeg build needs them |
| `app/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/{bin,lib}` | the base's working tree | DXC for the shader translator (ignored) |
| SDL3's render shaders' .hlsl, spirv-tools' build_overrides | the base's working tree | ignored upstream files the SDK's build may read |
| `out/renut_shader_dump`, `out/glslang` | banjo-nx's out/ | the game's shader containers (game data) and the host glslangValidator |
| `out/pc_user` | `D:\Banjo kazooie port\save\renut` | the user's save, with the DLC installed in it |

The Bash tool's PATH can lack the VS Installer folder, so vcvars64 fails ("'vswhere.exe' is not recognized"): builds run
with `PATH="/c/Program Files (x86)/Microsoft Visual Studio/Installer:$PATH"`.

## Build fixes

### The promoted indirect calls lost their sites (post_codegen stopped)

The first code generation with the base's generator (407 files, 39,926 functions, the same count as banjo-nx's tree)
stopped in post_codegen: "indirect_calls: 19 sites of app/indirect_promotions.txt are not in the generated code
(first: 821B5040 site 0)". tools/indirect_calls.py keyed a `bctrl` site by the last `DEFINE_REX_FUNC(sub_X)` above it,
and banjo-nx's config names 4,669 functions (appMainDraw, the CRT's, rex_XInputGetKeystroke ...), so a named function's
sites were counted as the previous sub_ function's. In banjo-nx's tree appMainDraw (0x821F1460) followed sub_821B5040
(a one-line tail jump) and its virtual call was "821B5040 site 0"; the base's generator lays the files out differently,
so those keys pointed at other functions or at none.

Fix (a handoff): every function is its own key, a named one by its address in `*_init.cpp`'s table
(`{ 0x821F1460, appMainDraw },`). `tools/indirect_promotions_rekey.py` maps an old list's keys through the tree it was
made for (banjo-nx's app/generated: 15,546 tagged sites): 22 of 287 entries moved to their own function, none lost, and
`indirect_calls.py --check` on the new tree promotes all 287 (26 with two or three targets).

### A duplicate after the merge, and a configure before the code generation

- `banjo_renut_tuning.cpp`: two `case 69` (banjo-nx's fetch spec name came in beside the base's, whose fetch spec is
  83); `render/pipeline.cpp`: `FetchSpecialised` twice (both trees had it). One of each kept.
- The first configure ran before any code generation, and `generated/rexglue.cmake` adds the generated sources (the
  recomp target and the include folder of `renut_pch.h`) only when `generated/sources.cmake` exists at configure time:
  the hot copies' files did not find `renut_pch.h`. A configure after the code generation (touching CMakeLists.txt)
  adds them. A new tree: run the code generation (or one failed build) first, then configure again.

## The calling convention (step 3; 2026-11-19, on the first generated tree)

`tools/cc_entry_census.py`: 39,926 functions, 13,994 reached through a pointer. **One** reads a scratch vector register
on entry: **sub_82402910 reads v0**. It is a message handler (a switch on the word at 0(r4)); at 0x82402D60 it stores v0
at +2784(r31), and on the path through 0x82402D48 (`bge cr6`: the halfword at 0(r11) is 3 or more) v0 is not loaded
first, so it stores what its caller left there (and passes r1+80, unwritten on that path, to sub_8254D2C8). Not provably
value-independent, so not left to the convention: `app/config/scratch_vectors_read_on_entry.txt` lists v0, and
tools/reg_liveness.py keeps the listed registers live at every indirect call (and at hook / import calls, which share
INDIRECT_USE), so the machine copies hand v0 over as the generated code does. Exact; a store more at such call sites.
The census names the listed register and then says none.

`tools/cc_exit_census.py`: 16,101 indirect call sites; 776 read an EXIT_DEAD register after the call before writing it
(4,514 reads). `tools/cc_exit_reads.py` (new) names each read's form; every one is value-independent:

| Reads | Form | Why it does not depend on the value |
|---|---|---|
| 3,857 | `twi 31,r0,22` (r0) | the debug trap: ppc_trap only logs (Katamari's six were the same) |
| 96 | `subfe rD,rX,rX` (r5-r10) | ~rX + rX + CA = CA - 1: the carry only |
| 58 | vupkd3d128 (v6-v12, v60-v62) | `REX_VUPKD3D_*`: the whole register from the source; defuse counts the destination as read |
| 42 | vmrghw (v5-v7) | the whole register from two other sources (simde unpack) |
| 3 | vsraw, vctsxs, vexptefp | the same: whole-register results of other sources |
| 1 site | fres | defuse does not know it (answers "every register"); it reads its one float source |
| 7 sites x 64 | `(tools/vec_lvlx_splat.py: ...)` | a post-codegen tool's note in the comments, parsed as an instruction |

So the exit bound holds for N&B with `SASRT_REG_LIVENESS_EXIT_ABI` on (the default). The census now knows these forms
(a handoff): it lists 631 sites, the traps alone.

## The PC port (step 2; 2026-11-19)

**Boot (run boot1, 170 s):** Renut on Vulkan, the user's save, title update 3 mounted ("XEX patch applied ... 0.0.3.1",
the update folder at `D:\Banjo kazooie port\update`), 1.0.0's font and dialogue boxes. A presses 40-94 s: the title, the
menus, the save loaded (93 s, "Saving content"), Showdown Town in the vehicle from about 140 s. The picture as renut-nx
1.0.2's. 138 error lines, all `NtQueryInformationFile(XFileXctdCompressionInformation) unimplemented` (a normal log has
about 127). tools/run_pc.ps1 first wrote no log at all: Start-Process does not quote, and Banjo's folders have spaces
(fixed; a handoff).

**Verify (run verify1, 480 s, tools/run_pc_verify.ps1, the same presses, then input_script_sticks from 160 s):** 533 hand-
picked copies reached (532 on), 487 of 506 bulk copies reached; 261 million calls compared, 134 million not comparable
(both forms stopped with the same stores); the stack forwards 9.3 million checked, 0 differences; the generated-code
self-checks (9.2 billion folded loads, 56.7 million promoted calls, 18 million owners from the PCR) 0 differences; the r1
mirror and link-register write-backs 0; the flush-mode flow 8.4 billion checks, 0 switches. **One copy differed:**
`sub_82C41318` (bulk), `f3: copy=0x8000000000000000 original=0xB6F4000000000000` three passes in a row (inputs r3 40EEF720
..., f1 3F6028F2A0000000), memory the same (204 bytes in 20 ranges): the original returns a negative single-precision
denormal in f3 where the copy returns -0.0. It is reached only through a one-line thunk (sub_82C42018, `b 0x82c41318`)
by pointer, so by the convention f3 is dead to its callers; f3 is in the guard's compare only as reg_liveness's margin
(f1-f4 kept as results). The flush modes before each f3 producer are the same in the two forms; the change since
renut-nx 1.0.2 (whose guard had this copy at 0) is ports 96 / 136's single-precision fused forms (REX_FMADDS_KNOWN). Not
explained, so not shipped: `off` in localized_bulk.txt (the generated original runs). Open: whether it is the guard's
two runs starting in different host flush states, or the fused form.

### Port 87's offsets, and the per-XDK values in shared files

See docs/ultimate-rebase-plan.md, "Found on the way". In short: port 87 (tiles once) now reads `native::off`; port
169's scaler copy is at N&B's +0x359C; the device's own declaration at +0x2FA8; port 138's viewport at 0x3170.

## The hot functions (step 6)

The base's machine copies (sasrt-nx's tools/localize_native.py) with N&B's own lists: app/src/cpu/hot/localized.txt
(the hand-picked copies: 533 reached) and localized_bulk.txt (506 bulk copies in 12 shards, from 1.0.2's console
profile), banjo_hand_natives.h (the XAudio filter's hand-written native) and the blur_* hand helpers. Two bulk
functions cannot be copies (sub_82335C28 reads the time base; sub_828177E8's callee has a switch line the tool does not
know). Checked by the guard twice: verify1 (exact build, 261.0 million calls compared) and verify_ne (the cleared
not-exact codegen, 300.4 million): two bulk copies are off for a single denormal against zero in a volatile float
register (sub_82C41318's f3, sub_8221BFA0's f6), the rest show no difference. On the Switch the copies are in place
(SASRT_HOT_IN_PLACE, the base's default there: no hook, no guard; an `off` copy keeps the generated original).

## PGO and LTO (step 7): ready, no profile yet

pgo/ and pgo-inplace/ are empty: Katamari's profiles went with its layer, and 1.0.2's profile (banjo-nx's untracked
pgo/) is for other code (the base's generator and passes changed most of the game code), so it would mostly be dropped
as mismatched. The tree builds both steps as docs/pgo-lto.md says; for this branch:

1. The instrumented NRO, with the copies in place as the release has them:
   `set BUILD_DIR=app\out\switch-pgo-gen` / `set "CMAKE_EXTRA=-DBANJO_PGO=generate"` / `set IN_PLACE=ON` /
   `tools\build_switch.bat`. The Switch defaults (CG-13, the cleared near-exact set, no trapping math) hold in every
   Switch folder, so the instrumented and the optimised build compile the same code.
2. Play it (the console, or Ryujinx with the banjo-ult data folder: the counts are code paths, not timing): the
   title, the menus, Showdown Town on foot and in the vehicle, a challenge. Then `python tools/pgo_merge.py <the pgo
   folder of streams> pgo-inplace --gen-build app\out\switch-pgo-gen\renut.nro` (one stream unless `--all`).
3. `tools\build_switch_release.bat`: app\out\switch-pgo-use\renut.nro (LTO, the profile, no game shader inside: it
   reads deko3d_glsl.bin beside itself, as the installer's package). Its log must have no "mismatch" line.

The everyday NRO of this package is app\out\switch (no PGO, no LTO, the shaders inside).
