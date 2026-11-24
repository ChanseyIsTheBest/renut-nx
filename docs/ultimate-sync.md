# The ultimate base in banjo-nx: where the branch is

| | |
|---|---|
| Base | renut_nx_ultimate_base, `D:\renut-ultimate\base`, branch `ultimate` |
| Taken up to | **b40a6f79** (2026-11-24, "docs: port 279's row"; ports 1-279; 223-249, 251, 274-277 are this tree's own) |
| How | the rebase of 2026-11-19 (docs/ultimate-rebase-plan.md), then tools/ult_port_sync.py (2026-11-24) |

## Taking the base's later ports

As the base's docs/ultimate/building.md section 7 says, a port commit at a time:

```
python tools/ult_port_sync.py --base D:\renut-ultimate\base --port "D:\Banjo kazooie port\banjo-nx-ult" --range b40a6f79..<base commit> --dry-run
```

then without `--dry-run`, resolve the diff3 blocks, build, test, and write the new commit here. The tool's default
`--hand` list (the per-XDK files) is right for banjo-nx: those are kept at N&B's title update and a port's change to
them comes in by hand.

What the tool cannot know about this tree:

- **The frame limiter.** The base's lives in Katamari's `app/src/katamari_keys.cpp` (skipped by the tool); banjo-nx's
  is `app/src/renut_engine/frameHooks.cpp` (`renutFrameLimit`, called from N&B's appMainDraw hook in FPS.cpp). A port
  that changes the base's limiter, or its `BlurSetFrameCapLive` / `BlurFrameCapNow`, is brought over by hand.
- **Game frames** are counted in N&B's Swap hook (me/banjo_me_hooks.cpp), not in Renut's (the base's port 156).
- **msaa_level** is N&B's key (renut_engine/hooks.cpp: 0 off or the game's own 2x), not the base's Renut sample cap;
  `MsaaLevel()` returns 4 here.
- **Per-XDK values in shared files**, which a port's diff may carry at Katamari's (XDK 2.0.5632) numbers:
  render/draw.cpp `kDeviceDeclarationOffset` (N&B 0x2FA8), render/draw.h's field boundaries, command_buffers.cpp's
  `kLastChunkBytes` (48), d3d.h's `kDeviceOffsetViewport` (0x3170), device.cpp's scaler copy (+0x359C). A new hex
  device offset in a port is checked against d3d_native.h's `native::off` and the base's katamari-notes zone table
  (docs/ultimate/katamari-notes.md: N&B = Katamari + 12, 16, 24, 28, 32 or 1708 by zone).
- **A/B numbers**: the base's fetch spec is A/B 83 (banjo-nx's was 69); 69 does nothing here.
- **The docs**: the base's pages are in docs/ultimate/ (the tool skips docs/ anyway).

## Record

| Date | Base commit | Ports | Notes |
|---|---|---|---|
| 2026-11-19 | 6cd02ae7 | 1-188 | the rebase (docs/ultimate-rebase-plan.md) |
| 2026-11-24 | b40a6f79 | 189-279 | ult_port_sync.py --skip 223-249 251 274-277 (banjo-nx's own handoffs: this tree keeps its own form of them). 33 conflicts in 21 files; the base's form taken for the SDK's game-file readers (ports 203/218/259/264/266), sasrt-nx's resolve carry (257) and render-thread rounds (260); Banjo's own kept: N&B's snapshot chunk (48 bytes), the Switch defaults (dynamic resolution on in both modes from 540, the docked heights), the frame limiter's default, input_script's started time. Hand files left out: ports 196 / 207 shader.cpp (GetFunction hooks, version 0x10 containers: not N&B's), 268 state.cpp (Katamari's viewport log). post_codegen run again (ports 191 / 199 / 200's tools). Port 279: the hand-over's two-destination loss fixed (see notes) |

A/B numbers since 2026-11-24: this tree's 320 (tile twin merge) and 322 (no late snap) are the base's 204 and 205 (ports 275 / 276, taken from this tree); 321 (EndTiling resolve once) is this tree's alone.
