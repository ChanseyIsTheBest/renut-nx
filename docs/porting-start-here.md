# Porting another Xbox 360 game from banjo-nx: start here

Status: 2026-10-28. banjo-nx (Banjo-Kazooie: Nuts & Bolts) is a ReXGlue port of an Xbox 360 game to the PC and the
Switch. Its renderer is reNut's renderer on a plume deko3d back end; its CPU side has the post-codegen passes, the
hot-function method and PGO + LTO. This page is the reading order for a new port that starts from it, what is current,
and the method rules that each cost Banjo a build or a console run.

## Read in this order

| # | Document | What it is | Status |
|---|---|---|---|
| 1 | `docs/porting-guide-renderer.md` | The renderer port: Renut on deko3d, what each piece buys, the porting order, pitfalls, and section 6b, the items measured SLOWER ON THE CONSOLE (port them off) | current |
| 2 | `docs/enhancements-reference.md` | Every key (CPU, GPU, renderer): what it does, why it is exact, what was measured. The table at the top lists what is off because it was slower | current |
| 3 | `docs/deko3d-renderer.md` | The deko3d back end feature by feature (resolves, depth hand-over, zcull, skin once section 22, recording) | current |
| 4 | `docs/record-time-draw-path.md` | The record-time draw path: setter marks, the fast path, the A/B tables | current |
| 5 | `docs/codegen-enhancements.md` | The post-codegen CPU passes (non-volatile locals, FP mode, inline memcalls, store cards ...) | 2026-10-19; the later CPU rounds are in `docs/enhancements.md` ("Bulk localized copies", 2026-10-22) |
| 6 | `D:\peggle 2\zumasrevenge-nx\docs\optimising-functions.md` and `hand-natives.md` | The hot-function method from console profiles: localized copies, write-backs of changed registers only, inlined callees, direct dispatch, hand-written natives; the cycle and the checks | current (2026-10-27; the newest copy of the bejeweled3-nx guide) |
| 7 | `docs/pgo-lto.md` | The PGO + LTO Switch build | current |
| 8 | `docs/four-core-mode.md` | The fourth core (NetherSX2_nx's layout) through a forwarder NPDM | current |
| 9 | `docs/deko3d-shader-optimisations.md`, `docs/shader.md` | The uam passes and the shader translator passes, shader sizes | 2026-10-20 / 21 |
| 10 | `docs/deko3d-port-faq.md`, `docs/deko3d-native-renderer-kit.md` | The kits given to other ports | 2026-10-19 / 20: their "no resolution changes" is superseded (internal resolution, then dynamic resolution with steps, below); MSAA is off (`disable_msaa`) |
| 11 | `docs/renut-renderer-port.md`, `docs/rexglue-api-renderer-guide.md` | How reNut's renderer was brought in, the API-level design | history; the MSAA notes are old (MSAA off now) |
| 12 | `docs/controls-and-keyboard.md` | sa2-nx's input driver, two players, the software keyboard | current |
| 13 | `docs/enhancements.md` | The long log of every round, console run and decision | current (read the parts you need) |

## Where Banjo stands (what to aim at)

- City, the heaviest scene, stock clocks (CPU 1020 MHz, GPU 460 MHz handheld), four-core: about 5,000-5,500 draws a
  frame, the render thread about 7.9-8.4 us a draw, GPU about 40 ms at 648 lines, 20-21 fps. At the user's maximum
  overclock 30 fps (the render thread scales linearly with the CPU clock, not with the RAM clock).
- Dynamic resolution: 540-720 handheld and 720-1080 docked, four steps between (`banjo_renut_dynamic_steps`), by the
  GPU's time against 34.5 ms; the HUD stays at the display's size.
- Decided defaults (2026-10-28 console checks): host draw packets OFF (neutral), replay look-ahead (29) and texture-slot
  skip (53) OFF (slower), shadow sort (40) OFF (slower), record prefetch (57) OFF (no effect), resident literals (34)
  ON, clear repeat ON, provisional pipeline count ON.
- The flat profile: the hottest render-thread line is 2 %. What is left is spread thinly; the big remaining costs are
  draw count (shadow passes) and a few heavy shaders (tessellated terrain, PS 9ED9E1A8 with VS F09B139E / 8EDDB323).

## Method rules (each cost Banjo a build or a run)

- **The console decides.** The PC and the emulator (x86 caches, a different GPU) are proxies. Judge every optimisation
  on the A57 at 1020 MHz.
- **In-run A/B:** 30 s halves, three rounds or more at one static spot, four-core through the forwarder, stock clocks,
  and a target that does nothing on the Switch in the list as the control (Banjo's 1, a Vulkan-only key). 15 s pairs
  had noise of about +-1.5 ms: they marked items slower that were not (34, 57).
- **Remove work, do not move it** to another thread or core. Prefetch and look-ahead schemes measured neutral or slower
  on the A57 almost every time; removing loads and instructions is what pays.
- **Exact (pixel-identical) by default.** Non-exact items (resolution, shadow culling, fur shell counts, SMAA) are
  configs the user decides on.
- **Line profiles:** give the hot renderer files `-g1` (Banjo: `BANJO_SWITCH_RENDERER_LINES`, cmake/banjo_renut_gpu.cmake)
  and symbolise with `C:\devkitPro\msys2\clang64\bin\llvm-symbolizer.exe` (one batched call). devkitA64's
  `addr2line` cannot read GCC's DWARF 5 line tables (every line comes out ":?"). Keep the source snapshot and the ELF of
  every package, so a console profile maps to exact lines.
- **Check crashes first** when reading console logs: the log's end, `rex_crash.log`, stall dumps, out-of-memory lines.
- **Do not edit sources while a build compiles**; read the build's output for its exit line first.

## Build and test essentials (this PC)

- PC build: `tools\pc_env.bat` (VS 18 vcvars64 with the VS-bundled CMake and Ninja; MSYS2's cmake on PATH breaks
  Windows paths), `C:\devkitPro\msys2\clang64\bin\clang++.exe` with target `x86_64-pc-windows-msvc`, LLD and
  `-march=x86-64-v2`. In a new shell the VS Installer folder may be missing from PATH (vswhere): prefix
  `PATH="/c/Program Files (x86)/Microsoft Visual Studio/Installer:$PATH"`. Codegen: `rexglue codegen <manifest>.toml`.
  `tools\build_pc.bat`.
- Switch build: `tools\build_switch.bat` (devkitA64 GCC); the dist and PGO folders as in `docs/pgo-lto.md`.
- Ryujinx (the user's patched Ryubing): `D:\New folder\Ryubing-src\ryubing\build\Ryujinx.exe` with
  `DOTNET_ROOT=D:\New folder\dotnet10`, started minimised as `Ryujinx.exe -r "D:\New folder\ryu-data\<port>" <nro>`.
  Each port has its own data folder under `D:\New folder\ryu-data` (banjo, sa2, peggle2, sasrt, futari ...). Stop only
  your own run's process, never Ryujinx by name. Start a run only when no Ryujinx runs and the free commit memory is
  11-13 GB or more; another port's emulator starting mid-run can still run the host out of memory ("Out of memory." in
  the run's stderr). Ryujinx keeps a host texture for every size of every target: resolution changes every few
  seconds exhaust the PC's video memory (`ErrorOutOfDeviceMemory`), so test resolution steps 20 s apart or more.
- Emulator checks: Banjo checks on the title-screen attract loop (captures every 15 s, verify keys on); shipped tomls
  keep verify keys off.
- Disk: check free space before codegen, builds and packages (codegen rewrites about 220 MB).
- Bash heredocs mangle backslashes in Python strings: write such files with the editor tools.

## Credits and licence

reNut's renderer (its author permits the user's ports to reuse it: credit it), sa2-nx (input, lazy mips, clear
repeat, colour hand-over, CPU items), NetherSX2_nx (four-core layout), battd_nx (the GPU arena), bejeweled3-nx and the
zuma ports (the hot-function method). GPL-3.0 where the sources are. Game data is never committed or distributed.
