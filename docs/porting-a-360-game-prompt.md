# Prompt: port an Xbox 360 game to the Switch

A fill-in prompt for starting a new port in a coding-agent session. Replace each placeholder below, then paste the whole
block into a new session.

It starts from [katamari-nx](https://github.com/ChanseyIsTheBest/katamari-nx), and step 1 of its reading list names that
port's docs. To start from another port, change the base's name and those pages too.

| Placeholder | What it is | Example (the Pac-Man port) |
|---|---|---|
| `<GAME>` | The game's name | Pac-Man Championship Edition DX |
| `<SHORT>` | A short name for the port's folder, notes and Ryujinx data | `pacman` |
| `<TITLE_ID>` | The game's title ID | `58410A8C` |
| `<STOP_TIME>` | When the session stops | 6:00 am (AEST) |
| `<GAME_FILES>` | The game's package or files | `D:\pacman\PAC MAN CE DX\58410A8C\000D0000\<package>` |
| `<UPDATE_AND_DLC>` | Its title update and DLC packages, or "none" | `D:\pacman\PAC MAN CE DX (1)\PAC MAN CE DX\58410A8C\00000002\AllinOnePack` |
| `<WORK_DIR>` | The folder this port's files go in | `D:\pacman` |
| `<BASE_DIR>` | Your copy of katamari-nx | `D:\beautifulkatamari\katamari-nx-public` |
| `<RENUT_DIR>` | Your copy of renut-nx | `D:\Banjo kazooie port\banjo-nx` |
| `<RYU_DATA>` | The folder of Ryujinx data folders | `D:\New folder\ryu-data` |

```text
Port the Xbox 360 game <GAME> to PC and the Nintendo Switch with ReXGlue, using the Beautiful Katamari port (katamari-nx)
as the base. Our primary objective is the Switch port; the PC port is only an intermediary step.
Work uninterrupted until <STOP_TIME> without stopping to ask me anything: make sensible decisions, write them down, and
keep going. Do the work in this main session (no subagents).

Inputs
- Game files (title ID <TITLE_ID>): <GAME_FILES>
- Title update and DLC: <UPDATE_AND_DLC>
  Extract packages (LIVE, PIRS or CON) with the base's tools/stfs_extract.py into <WORK_DIR>\game, <WORK_DIR>\update and
  <WORK_DIR>\dlc, outside the repo. Game data is never committed, uploaded or distributed.
- New port folder: <WORK_DIR>\<SHORT>-nx (create it as a new git repo from a copy of the base's files, not its history;
  commit locally as you go; never push unless I ask).
- Base, read-only (do not modify it): <BASE_DIR>
- Method docs, read-only (katamari-nx was built on renut-nx and does not carry them): <RENUT_DIR>\docs
- Ryujinx data folder for this port: <RYU_DATA>\<SHORT> (make it from <RYU_DATA>\katamari's system folders, keys and
  firmware, not its sdcard).

Read first, in this order
1. <BASE_DIR>\README.md, docs\building.md, docs\katamari-notes.md and docs\codegen-crash-fixes.md: the port you start
   from, what it found and what it changed from renut-nx.
2. <BASE_DIR>\docs\optimising-functions.md, all of it: the hot-function method. Its numbers are Katamari's; its rules and
   tools apply to this port.
3. <RENUT_DIR>\docs\porting-start-here.md: the reading order, what is current, the method rules, the build and test
   essentials of this PC.
4. <RENUT_DIR>\docs\porting-guide-renderer.md, all of it. Section 6b lists the items measured SLOWER ON THE CONSOLE: port
   them off.
5. <RENUT_DIR>\docs\enhancements-reference.md: the table at the top, then the keys you port.
6. <RENUT_DIR>\docs\deko3d-renderer.md, record-time-draw-path.md, codegen-enhancements.md, pgo-lto.md,
   deko3d-shader-optimisations.md, shader.md, controls-and-keyboard.md, and title-update-3.md if the game has a title
   update or DLC.
7. https://github.com/ChanseyIsTheBest/peggle2-nx/blob/main/docs/porting-a-360-game-prompt.md: the first ReXGlue port's
   order of work. It is older. Where docs differ, katamari-nx's win, then renut-nx's.
8. <RENUT_DIR>\docs\xdk-versions.md: the XDK build each of our ported games was made with.

The work, in order
1. Codegen and the PC port. Set up the project from katamari-nx's structure (CMake, the SDK, tools, the post-codegen
   passes). Take out what belongs to Katamari first: its function addresses and hooks, app/src/cpu/hot's lists and
   natives, pgo/, its icon, its settings and its DLC pack list (stale addresses also block copies: see "Stale mentions"
   in optimising-functions.md). Run codegen for <GAME>, and get it booting to gameplay on the PC with reNut's renderer
   (the Vulkan back end). Fix what stops it (imports, kernel calls, file system, input). Record every game-specific fix
   in docs/<SHORT>-notes.md.
2. The native deko3d renderer for the Switch. The base already has renut-nx's Renut renderer on plume's deko3d back end
   (app/src/renut_engine/gpu, app/thirdparty/plume, the uam shader pipeline, the shader package and cache tools). Get it
   drawing this game, with NVK not linked (deko3d only). Nothing Katamari decided carries over untested: the
   game-specific decisions of porting-guide-renderer.md section 2 ("What your game decides") each get a census first,
   including where this game keeps its shaders.
3. Test in Ryujinx. Build the NRO and run it in the patched Ryujinx with this port's own data folder, to the title screen
   and an attract loop or first gameplay, with captures every 15 s and the verify keys on. Fix every visual difference
   against the PC build.
4. Every enhancement that applies. Port renut-nx's CPU, GPU and shader enhancements (enhancements-reference.md); what the
   base already has still gets checked on this game. Each has its key and an in-run A/B target, checked in the emulator
   (verify keys: 0 differences). Leave off the ones marked SLOWER ON THE CONSOLE. The ones that change the picture become
   configs for me to decide on.
5. Hot functions, by katamari-nx's optimising-functions.md. Its two tracks replace any fixed top-5 or top-100 target.
   - Machine-optimise as many functions as possible, at every opportunity: every function any session samples (Ryujinx
     and the PC's profiler now, the console when I send a log), with tools/localize_native.py and
     tools/switch_hot_bulk.py --merge, then further eligible functions in batches, watching NRO size, free heap and build
     time. Profile in Ryujinx as well as on the PC, because we are optimising for ARM.
   - Hand-write a function only from console profile data, past the guide's gate, with the evidence recorded in hand.txt.
     A PC or Ryujinx profile never qualifies, however clear it looks, so expect no hand-written natives until I send a
     console profile.
   Verify each against the generated code before it is on (tools/run_pc_verify.ps1: 0 differences, with the counts showing
   it was reached).
6. PGO + LTO for the Switch build only when I say, in the guide's order: the last batch of machine copies, any
   hand-written natives, the instrumented build, my console session, then the optimised build (pgo-lto.md).
7. A package for my console by <STOP_TIME>:
   - the NRO
   - renut.toml with the shipped settings
   - renut_play.toml with the diagnostics off
   - renut_ab.toml with 30 s halves and a do-nothing control target
   - the ELF and a source snapshot, so a console profile maps to source lines
   - notes on what to test, and how to turn on a console profile

Rules
- Pixel-identical by default. Anything that changes the picture is a separate config and my decision.
- The console decides performance. The PC and the emulator are proxies: judge on the A57 at 1020 MHz.
- Remove work, do not move it to another thread or core.
- One Ryujinx run at a time, and only when none runs. Stop only your own run's process, never Ryujinx by name (other
  ports run theirs). Check free commit memory (8 GB or more) before a run.
- Never run two builds, never build during an emulator run, never edit sources while a build compiles.
- Check disk space before codegen, builds and packages. If the disk is full, stop and tell me; do not delete anything.
- No downloads, no system or firewall setting changes, no git push without my say.
- Bash heredocs mangle backslashes: write files with the editor tools.
- Credit reNut's renderer (its author permits reuse in my ports), katamari-nx, renut-nx, sa2-nx, NetherSX2_nx, battd_nx
  and the bejeweled3-nx / zuma ports. GPL-3.0 where the sources are.
- Keep docs/<SHORT>-notes.md and a progress log current as you go, so the final summary is accurate.

At <STOP_TIME> stop, and report:
- what works on the PC, in the emulator, and in the package
- what was measured
- what is next
- anything that needs my decision
```
