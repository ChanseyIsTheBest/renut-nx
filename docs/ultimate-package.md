# renut-nx on the ultimate base: the test package, its notes, and what the installer would need

The package of branch `ultimate` (banjo-nx-ult, 2026-11-19): renut-nx 1.0.2's game layer on the ultimate base at
6cd02ae7 (ports 1-188). The picture and the settings are 1.0.2's (rumble on, as the user asked); the CPU side has the
base's ports and the not-exact forms cleared on N&B (docs/banjo-near-exact.md).

## What is in it (out/package_ultimate_2026-11-19/)

| File | Where it goes | What it is |
|---|---|---|
| `switch/renut-nx/renut-nx.nro` | sdmc:/switch/renut-nx/ | the Switch NRO (deko3d) with the game's shaders inside (4,916, made from the user's own files with the base's translator): for the user's own card only, never published |
| `switch/renut-nx/deko3d_shaders.bin` | the same folder | the DKSH cache for those shaders (53,113 stage variants from 1.0.2's pipeline list) |

No `deko3d_glsl.bin` goes with it: the NRO uses an installed package before its own, and 1.0.2's (the installer's)
does not fit this build's translator, so 1.0.2's must be renamed away (step 1 below).
| `switch/renut-nx/renut.toml` | the same folder | 1.0.2's shipped settings with `log_level = "info"` (the 10 s reports) |
| `switch/renut-nx/renut_play.toml` | rename to renut.toml to play | 1.0.2's shipped settings exactly (logs off) |
| `switch/renut-nx/renut_ab.toml` | rename to renut.toml for the A/B | the in-run A/B (targets 83, 150, control 1; 30 s halves) |
| `renut-nx.elf` | not on the card | the ELF with symbols, for crash reports |
| `source/` | not on the card | the source snapshot (`git archive` of the commit; no game data) |

## On the console

1. Keep 1.0.2: rename its files in sdmc:/switch/renut-nx/ (`renut-nx.nro` -> `renut-nx-1.0.2.nro`, `deko3d_glsl.bin`
   -> `deko3d_glsl-1.0.2.bin`, `deko3d_shaders.bin` -> `deko3d_shaders-1.0.2.bin`). The user's card already has this
   done (2026-11-19, sys-ftpd), with the new NRO in place.
2. Copy the package's `switch/renut-nx/` files over. Start the game as 1.0.2 (the Sphaira forwarder: four cores).
3. Back to 1.0.2: rename the new files away and the `-1.0.2` ones back.

## The tests

- **The picture against 1.0.2:** the title, the menus, Showdown Town in the vehicle and on foot, a challenge.
  Ryujinx found no difference (see docs/banjo-ultimate-notes.md, "Ryujinx"); the console is the check that counts.
- **Speed against 1.0.2, ABBA:** the same route four times: 1.0.2, this, this, 1.0.2 (renut.toml with logs on in
  both). Each renut log's `[native] 10.0 s: N Swaps ...` lines give the frames; `rex_profile.log` (logs/rex/, only with
  logs on) the per-thread CPU. A gap of a few percent needs ABBA: the console's clocks and the dynamic resolution
  move the figures between runs.
- **The in-run A/B** (renut_ab.toml): each target is switched on and off every 30 s in one run; the log's `[ab]` lines
  give the two halves' frame times (target 1 is the control: a switch that changes nothing, so its gap is the noise).
- **What to read first in the logs:** a crash or stall (the log's end, logs/rex/rex_crash.log, stall dumps), out of
  memory (`Memory:` lines, `heap users`), shader compiles in play (`[deko3d]` lines: with the cache, about none).

## What the installer (banjo-nx-installer) would need for this build (listed only; nothing there was changed)

The installer makes deko3d_glsl.bin and the DKSH cache in the player's browser with banjo-nx's (1.0.2's) shader tools.
This build's translator writes other GLSL (none of 1.0.2's 28,614 cache entries matches its variants), so a package
made by the installer as it is would not fit this NRO (every variant compiled on the console). It would need:

1. `wasm/renut_hlsl`: the base's XenosRecomp (app/thirdparty/reblue-XenosRecomp of this branch) built to WebAssembly.
2. `py/`: this branch's `deko3d_glsl.py`, `deko3d_glsl_opt.py`, `early_alpha.py`, `renut_early_alpha.py` and the
   shader lists (gpr32, lightbranch, nullps, skinonce): the GLSL must come out byte for byte as the PC tools make it
   (the installer's own check: its package against the native one).
3. `wasm/uam`: uam from the base's sources (ports 162 / 163 changed it); the DKSH must match the console's compile.
4. `release/`: the distribution NRO of this branch (tools/build_switch_release.bat: LTO, the PGO profile, no game
   shader inside), its `shader_common.h`, `variants.txt` (`deko3d_variant_cache.py --export-variants` from the new
   package), `deko3d_glsl.dict` (retrained on the new GLSL), `pipeline_list.bin` (1.0.2's works with `--sasrt-guess`; a
   list from a console run of this build is better), `manifest.json`'s sizes and hashes, and `renut.toml` (1.0.2's
   keys; nothing new for the player).
5. The NRO keeps `CompressInstalledPackage` (the installer's raw frames), so the page's package format is unchanged.
6. The README's version text.
