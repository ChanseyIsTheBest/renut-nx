# Prompt: port another Xbox 360 game from banjo-nx (2026-10-28)

Fill in the four placeholders, then paste everything below the line into a new session.

---

Port the Xbox 360 game **<GAME>** to the PC and the Nintendo Switch with ReXGlue, using the banjo-nx port as the base.
Work uninterrupted until 8:00 am (AEST) without stopping to ask me anything: make sensible decisions, write them down,
and keep going. Do the work in this main session (no subagents).

**Inputs**
- Game files: `<GAME_DIR>` (the extracted XEX and data). Game data is never committed, uploaded or distributed.
- New port folder: `<PORT_DIR>` (create it as a git repo; commit locally as you go; never push unless I ask).
- Base, read-only (do not modify it): `D:\Banjo kazooie port\banjo-nx`.
- Ryujinx data folder for this port: `D:\New folder\ryu-data\<RYU_FOLDER>` (make it from `ryu-data\banjo`'s system
  folders, keys and firmware, not its sdcard).

**Read first, in this order**
1. `D:\Banjo kazooie port\banjo-nx\docs\porting-start-here.md`: the reading order, what is current, the method rules,
   the build and test essentials of this PC.
2. `banjo-nx\docs\porting-guide-renderer.md`, all of it. Section 6b lists the items measured SLOWER ON THE CONSOLE: port
   them off.
3. `banjo-nx\docs\enhancements-reference.md`: the table at the top, then the keys you port.
4. `banjo-nx\docs\deko3d-renderer.md`, `record-time-draw-path.md`, `codegen-enhancements.md`, `pgo-lto.md`,
   `four-core-mode.md`, `deko3d-shader-optimisations.md`, `shader.md`, `controls-and-keyboard.md`.
5. `D:\peggle 2\zumasrevenge-nx\docs\optimising-functions.md` and `hand-natives.md`: the hot-function method.
6. `D:\peggle 2\peggle2-nx\docs\porting-a-360-game-prompt.md`: the first ReXGlue port's order of work. It is older:
   where it and banjo-nx's docs differ, banjo-nx's win.

**The work, in order**
1. **Codegen and the PC port.** Set up the project from banjo-nx's structure (CMake, the SDK, tools, the post-codegen
   passes), run codegen for <GAME>, and get it booting to gameplay on the PC with reNut's renderer (the Vulkan back
   end). Fix what stops it (imports, kernel calls, file system, input). Record every game-specific fix in
   `docs/<game>-notes.md`.
2. **The native deko3d renderer for the Switch.** Bring over banjo-nx's Renut renderer on plume's deko3d back end
   (`app/src/renut_engine/gpu`, `app/thirdparty/plume`, the uam shader pipeline, the shader package and cache tools),
   with NVK not linked (deko3d only). The game-specific decisions of porting-guide-renderer.md section 2 ("What your
   game decides") each get a census first.
3. **Test in Ryujinx.** Build the NRO and run it in the patched Ryujinx with this port's own data folder, to the title
   screen and an attract loop or first gameplay, with captures every 15 s and the verify keys on. Fix every visual
   difference against the PC build.
4. **Every enhancement that applies.** Port banjo-nx's CPU, GPU and shader enhancements (enhancements-reference.md),
   each with its key and an in-run A/B target, checked in the emulator (verify keys: 0 differences). Leave off the
   ones marked SLOWER ON THE CONSOLE. The ones that change the picture become configs for me to decide on.
5. **Hot functions.** From a profile of the running game (the PC's sampling profiler first; a console profile when I
   send one):
   - Hand-optimise the top 5 game functions as hand-written natives with their guards and checks (hand-natives.md).
   - Machine-optimise the top 100 with optimising-functions.md (`tools/localize_native.py`,
     `tools/switch_hot_bulk.py`): localized copies, write-backs of the changed registers only, small callees inlined,
     direct dispatch.

   Verify each against the generated code (compare mode) before it is on.
6. **PGO + LTO** for the Switch build (pgo-lto.md), once the NRO runs.
7. **A package for my console by 8 am:**
   - the NRO
   - `renut.toml` with the shipped settings
   - `renut_play.toml` with the diagnostics off
   - `renut_ab.toml` with 30 s halves and a do-nothing control target
   - the forwarder NPDM for four-core mode
   - the ELF and a source snapshot, so a console profile maps to source lines
   - notes on what to test

**Rules**
- Pixel-identical by default. Anything that changes the picture is a separate config and my decision.
- The console decides performance. The PC and the emulator are proxies: judge on the A57 at 1020 MHz.
- Remove work, do not move it to another thread or core.
- One Ryujinx run at a time, and only when none runs. Stop only your own run's process, never Ryujinx by name (other
  ports run theirs). Check free commit memory (11-13 GB or more) before a run.
- Never run two builds, never build during an emulator run, never edit sources while a build compiles.
- Check disk space before codegen, builds and packages. If D: is full, stop and tell me; do not delete anything.
- No downloads, no system or firewall setting changes, no git push without my say.
- Bash heredocs mangle backslashes: write files with the editor tools.
- Credit reNut's renderer (its author permits reuse in my ports), sa2-nx, NetherSX2_nx, battd_nx and the
  bejeweled3-nx / zuma ports. GPL-3.0 where the sources are.
- Keep `docs/<game>-notes.md` and a progress log current as you go, so the morning summary is accurate.

**At 8 am** stop, and report:
- what works on the PC, in the emulator, and in the package
- what was measured
- what is next
- anything that needs my decision
