# renut-nx on the ultimate base: the test package, its notes, and what the installer would need

**2026-11-23: renut-nx 1.0.3, the release of this branch.** How it was made (the installer's list at the end of this
page, done):

- The NRO: VERSION 1.0.3 (app/CMakeLists.txt) and the installer package tag `renut8-ultglsl1`
  (banjo_renut_deko3d.cpp kInstalledTag; the installer's lib/package.js INSTALLED_TAG), so a 1.0.2 package is refused.
  Built in the everyday folder (app/out/switch: the PGO profile of pgo-inplace, LTO, copies in place) reconfigured with
  `-DBANJO_RENUT_RELEASE_SHADERS=ON`, with app/out/renut_switch_release made by
  `tools/renut_switch_shader_cache.py --pc app/out/pc --only <run-time captures> --out-dir app/out/renut_switch_release`
  and `tools/deko3d_shader_package.py --glsl out/deko3d_shaders/glsl_opt --only <the same> --raw --out
  app/out/renut_switch_release` (the 185 shaders the game makes at run time: banjo-nx's app/out/pc-me/shader_capture).
  NOT renut_release_shaders.py (katamari-nx's: it keeps the whole shader table and builds no package in, so the
  installer would have no run-time shaders). Set the folder back with `-DBANJO_RENUT_RELEASE_SHADERS=OFF` for the
  everyday build.
- The variants (the installer's release/variants.txt): `tools/deko3d_variant_cache.py --glsl out/deko3d_shaders/glsl_opt
  --list <the console's pipeline_list.bin> --generic --plain-guess app/out/renut_switch/shader_cache_switch.cpp
  --sasrt-guess --round-guess --runtime-log <every console log> --export-variants ... --dry-run`: 113,623 (no
  `--noearly-ab`: its variants serve A/B 151 only). The export's key check is FNV in Python: about 40 minutes.
- The dictionary (release/deko3d_glsl.dict): the native package's (app/out/renut_switch/deko3d_glsl.bin, the bytes from
  offset 32, its header's size).
- The installer: py/ from tools/ (with twinsel_shaders.txt and noearly_keep.txt), the translator and uam built to
  WebAssembly from this branch (the translator's front end sets userClipPlane as main.cpp does), the cache index
  (deko3d_shaders.bin.idx) written by the page, and the variant texts made when compiled (8 GB of text otherwise).


**2026-11-20: out/package_ultimate_2026-11-20/** replaces the package below for testing: the NRO with PGO and LTO
(md5 84a50b1d6fe14ea1d1bbeafba5970199, 118,140,646 bytes, every library but uam in both; ELF beside it), the DKSH cache
v3 (105,814 variants, 1,184,506,656 bytes: the run-time keys the renderer asks for; docs/banjo-ultimate-notes.md, "The
console regression"), renut.toml = the shipped settings with logs on and banjo_deko3d_log_misses on (it names the
variants the cache still lacks in deko3d.log: send them back for the next cache). The same three-file swap on the card
(renut-nx.nro, deko3d_shaders.bin; 1.0.2's deko3d_glsl.bin renamed away).

The package of branch `ultimate` (banjo-nx-ult, 2026-11-19): renut-nx 1.0.2's game layer on the ultimate base at
6cd02ae7 (ports 1-188). The picture and the settings are 1.0.2's (rumble on, as the user asked); the CPU side has the
base's ports and the not-exact forms cleared on N&B (docs/banjo-near-exact.md).

## What is in it (out/package_ultimate_2026-11-19/)

Built 2026-11-19 21:41 from commit beb56743 (branch ultimate): renut-nx.nro md5 fe622e97bfec57152af032f72d3fdf45, 110,227,174 bytes;
the ELF beside it (`renut-nx.elf`, unstripped, for crash addresses); `source_beb56743.zip` (`git archive` of that
commit: no generated code, no game data); `README-test.md` (this page); the handoff note and patch.

| File | Where it goes | What it is |
|---|---|---|
| `switch/renut-nx/renut-nx.nro` | sdmc:/switch/renut-nx/ | the Switch NRO (deko3d) with the game's shaders inside (4,916, made from the user's own files with the base's translator): for the user's own card only, never published |
| `switch/renut-nx/deko3d_shaders.bin` | the same folder | the DKSH cache for those shaders (53,113 stage variants from 1.0.2's pipeline list) |

No `deko3d_glsl.bin` goes with it: the NRO uses an installed package before its own, and 1.0.2's (the installer's)
does not fit this build's translator, so 1.0.2's must be renamed away (step 1 below).
| `switch/renut-nx/renut.toml` | the same folder | 1.0.2's shipped settings with `log_level = "info"` (the 10 s reports) |
| `switch/renut-nx/renut_play.toml` | rename to renut.toml to play | 1.0.2's shipped settings exactly (logs off) |
| `switch/renut-nx/renut_ab.toml` | rename to renut.toml for the A/B | the in-run A/B (target 83, control 1; 30 s halves; 150 left out: its off half needs variants the cache lacks) |
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
