# Four-core mode (the system core)

Ported 2026-10-27 at the user's request from **NetherSX2_nx's 4-core support** (NaGaa95,
https://github.com/NaGaa95/NetherSX2_nx, MIT licence; release 1.2.5, commit `f33e41e` "Use all four CPU cores when the
NPDM grants them"). The layout, the fallback and the priorities follow its `source/pthr.c` (`core_init_once`,
`pthr_pin_bg_core`, `pthr_set_priority`) and its forwarder's `restoreMainThreadAffinity` (`launcher/fwd/main.c`).

## What it does

A Switch application normally gets cores 0-2 (`svcGetInfo(InfoType_CoreMask)` = `0x7`); core 3 belongs to the system.
A process whose NPDM grants CPU ids 0-3 gets all four (`0xF`). Then, as NetherSX2_nx does, the **top core is kept for
background work** and the game's own threads stay on cores 0-2:

| Thread | Where | Priority | Why |
|---|---|---|---|
| Audio output (`SwitchAudioSystem::OutputThreadMain`) | core 3 alone | 0x2A | feeds audout; above the others on that core |
| Audio Worker, XMA Decoder | core 3 alone | 0x2B | as before (`SwitchPriorityFor`) |
| XAudio supervisor / peon threads (the mixer: the voice filter, the reverb, the converters, the router) | core 3 alone | guest 0x3B | moved as they start (hooks on `XAUDIO::CEngine::SupervisorProcessingThreadEntryPoint` / `PeonProcessingThreadEntryPoint`) |
| uam shader compiles, pipeline prebuild workers | core 3 alone | as before (0x3B; a waiting thread lends its own) | `banjo_system_core_compiles` |
| Everything else | cores 0-2 | unchanged | — |

Horizon does not preempt between threads of equal priority (other than 0x3B, which it time-slices), so the threads
that share core 3 have distinct priorities where they must interrupt one another (NetherSX2_nx's rule).

In the city profile of 2026-10-27 the threads moved cost about **0.4 of a core** (mixer 25 %, Audio Worker 9 %, XMA
Decoder 7 %, output thread ~1 %) plus the background compiles while they run. The user's estimate is that core 3 has
~55 % free, so they fit; the report shows what is left (below).

### How the game's threads are kept off core 3

libnx's `pthread_create` gives every new thread the process's **whole** mask (`__syscall_thread_create` in `newlib.o`:
`svcSetThreadCoreMask(handle, -1, InfoType_CoreMask)`), and NetherSX2_nx's forwarder hands the main thread over with the
whole mask too. So `svcSetThreadCoreMask` is wrapped at link time (`--wrap=svcSetThreadCoreMask`,
`sdk/cmake/rexglue_switch.cmake`, like the existing `threadCreate` wrap): `__wrap_svcSetThreadCoreMask`
(`sdk/src/core/threading_switch.cpp`) takes core 3 out of every mask of more than one core (mode 0 or 1). A request for
core 3 **alone** (the moves above) goes through. `RexSwitchInitSystemCore` (called from `main` right after the cvars
are read) also takes it out of the main thread's mask.

`RexSwitchPinCurrentThreadToCore(3)` works when the process has core 3 (it goes through the move), so `banjo_ring_core`
and friends could name it, but nothing does by default.

## Keys

| Key | Default | What it does |
|---|---|---|
| `banjo_system_core` (Banjo/Threads, init only) | `1` | 0 = leave core 3 to the system; 1 = the background threads above on core 3, every other thread on 0-2 (NetherSX2_nx's layout); 2 = as 1, and every other thread allowed on core 3 too (Horizon moves a ready thread to an idle core; untested). No effect with three cores. |
| `banjo_system_core_compiles` (Banjo/Threads, init only) | `true` | the uam compiles and the prebuild workers go to core 3 instead of `banjo_background_compile_core` when the process has it. |
| `banjo_forwarder_program_id` (Banjo/Threads, init only) | `"05D6EA1333487000"` | the forwarder whose four-core NPDM Banjo writes (below); empty: never. |

## What the log shows

- When the render thread starts: `[banjo] system core: process cores 0xF, system core 3 (...); forwarder NPDM ...`.
  With the usual launch: `process cores 0x7, system core -1` and nothing else changes.
- `[banjo] XAudio supervisor thread on the system core (banjo_system_core)` when the mixer thread starts.
- In `rex_profile.log`'s 10 s report: `system core 3: idle N% (...)`: the share of core 3 that **nobody** used over the
  interval (InfoType_IdleTickCount, which only a thread on that core can read: the audio output thread samples it every
  100 ms). Each thread's line shows its mask (`mask 0x8` for the moved ones).
- The `uam thread: core 3, ...` line of the deko3d log.

If the idle share falls near 0 % and the audio crackles, the system needs more of core 3 than expected: set
`banjo_system_core_compiles = false` first, then `banjo_system_core = 0`.

## How to get the four cores

The grant is the launching process's NPDM; the usual hbmenu / title-takeover launch gives `0x7`. Banjo's forwarder
(the user's, program id `05D6EA1333487000`) gets `0xF` through **Atmosphere's ExeFS file replacement**: a
`main.npdm` at `sdmc:/atmosphere/contents/05D6EA1333487000/exefs/main.npdm` replaces the one inside the forwarder's
ExeFS when Atmosphere's loader starts it.

**Banjo writes that file itself** (`RexSwitchEnsureForwarderNpdm`, `sdk/src/core/threading_switch.cpp`): at startup,
when the process's program id (`svcGetInfo(InfoType_ProgramId)`) is `banjo_forwarder_program_id`, it writes the NPDM
there if it is missing or different. So: launch the forwarder once (three cores; the file is written), launch it again
(four cores). Under any other program id (a game's, through title takeover) nothing is written: the file would replace
that game's NPDM. The package also carries the same file under `COPY_TO_SD_ROOT/` for copying by hand.

The NPDM (`tools/forwarder/banjo_forwarder.json`, built with devkitPro's `npdmtool` into `tools/forwarder/main.npdm`,
embedded as `sdk/src/core/forwarder_npdm.inc`; the writer patches the running program id into ACI0 and ACID's range):
NetherSX2_nx's forwarder NPDM (`launcher/fwd/hbl.json`) with these values: CPU ids 0-3, default core 0, thread
priorities 28-59, main thread 0x100000 bytes at priority 44, every syscall 0x01-0xBF, all services, all filesystem
permissions (content owner 0x0100000000001000), application type 1, kernel 0x30, 512 handles, the read-only map
region of type 1, debug flags as theirs (force_debug_prod). One difference: address space type 3 (39-bit, what retail
games use, so the address space Banjo has under title takeover) where NetherSX2_nx's has 1 (36-bit). Like every
homebrew NPDM its ACID is unsigned: it loads where the forwarder's own NPDM loads (the same loader patches).

**renut-nx 1.0.0 (2026-11-01): any player's Sphaira forwarder.** The players make the forwarder in Sphaira (X,
Install Forwarder, Address Space 39-bit, Create Forwarder). Sphaira's program id depends on the NRO's path
(sphaira `source/owo.cpp`): the first 8 bytes of the SHA-256 of the path written twice (the forwarder's arguments
default to the path), little-endian, as `05xxxxxxxxxxx000` (homebrew) or, in its native-game mode, `01xxxxxxxxxxx000`
with bit 12 clear. The user's `05D6EA1333487000` is that of `sdmc:/switch/banjo-nx/banjo-nx.nro`. renut-nx computes
the ids of its own `argv[0]` (with and without `sdmc:`, both modes) and writes the NPDM for them as for
`banjo_forwarder_program_id` (`SphairaForwarderOfThisNro`, `threading_switch.cpp`); for
`sdmc:/switch/renut-nx/renut-nx.nro` they are `059270E176E83000` and `019270E176E82000`. A retail game's id (title
takeover) never matches.

The log line at the render thread's start says what happened:
`[banjo] system core: process cores 0xF, system core 3 (banjo_system_core 1; game threads on 0x7); forwarder NPDM in
place: sdmc:/atmosphere/contents/05D6EA1333487000/exefs/main.npdm`. With `written (launch the forwarder again ...)`
the cores come at the next launch; with `process cores 0x7` and `in place`, Atmosphere did not take the file (check the
folder name and that the forwarder is that program id).

`banjo_system_core_test` (Banjo/Debug, off): the layout on three cores (the highest, core 2, as the "system" core), to
run the moves, the hooks and the mask wrap in an emulator.
