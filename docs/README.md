# renut-nx documentation

Developer notes for renut-nx, the Switch port of Banjo-Kazooie: Nuts & Bolts. To install or play the game, see the
[main README](../README.md) instead.

renut-nx draws with [reNut](https://github.com/masterspike52/reNut)'s Direct3D-level renderer on deko3d, the Switch's native
GPU API, and runs title update 3 with its DLC. These pages cover that renderer and the CPU work around it.

The earlier PM4 renderer is not documented here. Its code is still in the NRO, but idle: nothing on the Switch uses it or
its keys ([deko3d-renderer.md](deko3d-renderer.md), section 23). Its pages are in the git history, up to tag `v1.0.1`.

Many pages were written as the work happened, so a later section can replace a detail in an earlier one. When two pages
disagree, trust the newer one.

## Where to start

| You want to | Read |
|---|---|
| Know what a `renut.toml` key does | [enhancements-reference.md](enhancements-reference.md) |
| Understand the renderer | [deko3d-renderer.md](deko3d-renderer.md), then [record-time-draw-path.md](record-time-draw-path.md) |
| Understand the shaders | [shader.md](shader.md), then [deko3d-shader-optimisations.md](deko3d-shader-optimisations.md) |
| Make a game function faster | [Beautiful Katamari's optimising functions guide](https://github.com/ChanseyIsTheBest/katamari-nx/blob/main/docs/optimising-functions.md) |
| Build the release NRO | [pgo-lto.md](pgo-lto.md) |
| Change the title update, DLC or language handling | [title-update-3.md](title-update-3.md) |
| Port another Xbox 360 game from this one | [porting-start-here.md](porting-start-here.md) |
| See which XDK a game was built with | [xdk-versions.md](xdk-versions.md) |

## The renderer

| Page | What it covers |
|---|---|
| [deko3d-renderer.md](deko3d-renderer.md) | The deko3d back end, feature by feature: mapping plume to deko3d, shaders, memory, resolves, zcull, resolution, and the notes for releases 1.0.0 and 1.0.1 |
| [renut-renderer-port.md](renut-renderer-port.md) | How reNut's renderer is built in and fitted to the A57: the import tool, the hooks, exactness and memory rules |
| [record-time-draw-path.md](record-time-draw-path.md) | Translating the game's draw state once, when it is recorded, instead of on every draw: the plan, the work and the A/B results |
| [shader.md](shader.md) | How shaders get from the Xbox 360 to the Switch's GPU, what the translator does to them, the specialization constants, and how to measure them |
| [deko3d-shader-optimisations.md](deko3d-shader-optimisations.md) | The GLSL passes and the optimisations added to uam, deko3d's shader compiler |

## Keys and CPU work

| Page | What it covers |
|---|---|
| [enhancements-reference.md](enhancements-reference.md) | Every run-time key that acts on this renderer and the CPU: what it does, why it keeps the picture exact, how it is checked, what was measured |
| [enhancements.md](enhancements.md) | The history of the CPU work: localized copies, hand-written natives, bulk copies and the later rounds |
| [Optimising game functions](https://github.com/ChanseyIsTheBest/katamari-nx/blob/main/docs/optimising-functions.md) (katamari-nx) | Beautiful Katamari's guide to making a game's own functions faster on the Switch: machine copies of as many functions as possible, and hand-written ones only from console profile data. It uses that port's tools and numbers |
| [codegen-enhancements.md](codegen-enhancements.md) | Build-time changes to the generated game code: generator options and post-codegen passes |
| [renut-cpu-work-from-sa2.md](renut-cpu-work-from-sa2.md) | The CPU work taken from sa2-nx, what changed for this game, and what was left out |
| [four-core-mode.md](four-core-mode.md) | Using the fourth CPU core through a forwarder, following NetherSX2_nx's layout |
| [pgo-lto.md](pgo-lto.md) | The link-time and profile-guided optimised Switch build |

## The game

| Page | What it covers |
|---|---|
| [title-update-3.md](title-update-3.md) | Moving the port to title update 3, installing the DLC, and the `language` key |
| [controls-and-keyboard.md](controls-and-keyboard.md) | The input driver from sa2-nx, two players, and the Switch software keyboard |

## Porting another game

Start at [porting-start-here.md](porting-start-here.md): it gives the reading order, says which guides are current, and
lists the method rules.

| Page | What it covers |
|---|---|
| [porting-guide-renderer.md](porting-guide-renderer.md) | Moving a ReXGlue Switch port to this renderer: what each piece gains, the order of work, and (section 6b) what measured slower on the console |
| [deko3d-native-renderer-kit.md](deko3d-native-renderer-kit.md) | The deko3d kit given to other ports: which parts to reuse, the route, and the traps. Older than the current renderer |
| [deko3d-port-faq.md](deko3d-port-faq.md) | Answers to the questions other ports asked while following the kit. Older than the current renderer |
| [porting-a-360-game-prompt.md](porting-a-360-game-prompt.md) | A fill-in prompt to paste into a new coding-agent session to start a port |
| [xdk-versions.md](xdk-versions.md) | The XDK build, title ID and kernel version of 19 of our Xbox 360 ports' games, and how they were read |

## Words used in these pages

- **banjo-nx** is this project's former name. Repository paths such as `dist/switch/banjo-nx/` keep it.
- **Package** or **round** followed by a number or date means one numbered test build.
- **Exact** or **pixel-identical** means a change that must leave the picture the same.
- **A/B** is a comparison with one key on and off, often inside a single run.
- **uam** is deko3d's shader compiler. **DKSH** is its compiled shader format.
- **EDRAM** is the Xbox 360's render-target memory. The game still thinks in terms of it, even though the renderer does not
  emulate it.
- **NVK** and **NAK** are the Vulkan driver and its shader compiler, used before deko3d. A few pages mention them when
  explaining why something was done.
- Paths starting `D:\` or `C:\` are on the author's PC. They are not part of this repository, and that includes sister
  ports' folders.
