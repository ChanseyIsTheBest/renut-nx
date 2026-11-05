# renut-nx

**Banjo-Kazooie: Nuts & Bolts on the Nintendo Switch** (homebrew), version 1.0.2, by ChanseyIsTheBest. Formerly
banjo-nx.

renut-nx is a Switch port of [reNut](https://github.com/masterspike52/reNut), masterspike52's static recompilation of
the Xbox 360 game, built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). It draws with reNut's
Direct3D-level renderer on deko3d, the Switch's native GPU API, and runs the game's title update 3 with its DLC.

No game data is included. The game's files, its shaders and the update come from your own copy.

## Install

Use **[renut-nx-webinstaller](https://chanseyisthebest.github.io/renut-nx-webinstaller/)**: a web page that builds the Switch package from your Xbox 360 disc image (or the
folder it was extracted to), title update 3's package and, if you have it, the DLC package. It runs in your browser
and uploads nothing.

1. Open the web installer (https://chanseyisthebest.github.io/renut-nx-webinstaller/), choose your game, then title update 3's package (`TU_16L61VD_0000004000000.00000000000O1`)
   and the DLC (`E41F20AAF7094B9AFCA8CC90489D689D72A5062B4D`, optional).
2. Press **Create renut-nx.zip** and wait: the page makes the game's shaders and compiles them for the Switch.
3. Extract `renut-nx.zip` to the root of the SD card: it makes `switch/renut-nx/`.
4. In **Sphaira**, find Banjo-Kazooie Nuts & Bolts, press X to open Options and choose **Install Forwarder**. Make
   sure **Address Space** is set to **39-bit**, choose **Create Forwarder**, then start the game from the Home Screen.

The forwarder is how the game gets all four CPU cores (it cannot through a title takeover): started through it,
renut-nx writes an NPDM for the forwarder's program id (Atmosphère's ExeFS replacement), and from the next start on it
moves audio and the shader compiler to the fourth core (docs/four-core-mode.md).

## Settings

`switch/renut-nx/renut.toml` lists the settings meant to be changed, by category: the language (`language`:
`"system"`, the default, follows the Switch's system language; or one of the game's 15 by name), MSAA and SMAA, the
dynamic resolution (handheld 540p-720p, docked 540p-1080p), fur detail, 1.0.0's font and dialogue boxes, the frame cap,
four-core mode and the controls. Everything else is built in.

## Logs

Off, except crash logs (`switch/renut-nx/logs/rex/rex_crash.log`). For a bug report, set `log_level = "info"` in
renut.toml and send the `logs/` folder.

## Building

See `app/README.md` (reNut's) and `docs/` (the Switch port: `docs/deko3d-renderer.md`, `docs/title-update-3.md`,
`docs/enhancements-reference.md`). The distribution NRO carries none of the game's shaders but the ones the game
makes at run time; the web installer makes the rest.

## Credits

- [reNut](https://github.com/masterspike52/reNut) by masterspike52: the recompilation and its renderer, used here with
  the author's permission.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) and its contributors.
- [plume](https://github.com/renderbag/plume) (rendering abstraction), with a deko3d back end written for this port.
- deko3d and uam by fincs and the devkitPro team; libnx; Mesa's GLSL compiler inside uam.
- XenosRecomp (hedge-dev) in reNut's version.
- Work shared between sister ports: sa2-nx, futari-nx, sasrt-nx, katamari-nx, kameo-nx and the Torchlight port's findings (2 MB
  blocks), UnleashedRecomp-NX, NetherSX2_nx's
  four-core layout, nfsmw-nx (StevensND), battd_nx; keflings-nx-installer's Xbox 360 package reader in the web
  installer. Details in `docs/` and in each file's comments.
- Banjo-Kazooie: Nuts & Bolts is © Microsoft and Rare. This project is not affiliated with them.
