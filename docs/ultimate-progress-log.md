# banjo-nx on the ultimate base: progress log

Times AEST. One line per step; the details are in docs/banjo-ultimate-notes.md, docs/banjo-near-exact.md and the commits.

## 2026-11-19

- 09:40 Read the base's README, docs (building, changelog, ultimate-fixes, off-by-default, the porting prompt, xdk-notes,
  katamari-notes' layout sections). The base fetched into banjo-nx's object store as `refs/ultbase/ultimate` (no remote,
  master untouched).
- 10:05 Inventory: the base's first commit is banjo-nx 894d3256 plus 13 files banjo-nx committed later in 1.0.0; the
  Katamari era ends at 701c9d32 (katamari-nx 1.0.1), the ultimate ports start at 6929e720. Classes of every file
  (tools/ult_rebase_classify.py). Worktree `D:\Banjo kazooie port\banjo-nx-ult`, branch `ultimate` from master.
- 10:10 Commit 2c99d6de: the base's tree at 6cd02ae7.
- 10:12-10:35 tools/ult_rebase_apply.py: Katamari's layer out (6f00644a), Banjo's game layer back (fc79fa18), Renut at
  Banjo's XDK (7cb428f4: 6 conflict blocks; port 87's Viva Pinata offsets, port 169's scaler copy, the viewport fixed),
  Banjo's later work in the shared files (77cceaf5: 78 conflict blocks in 20 files), docs (1f60a14c).
- 10:40 PC build: the first configure stopped at `generated/rexglue.cmake` (generator output, untracked in both trees:
  the base's copy, with port 69's PCH byproduct, put in app/generated) and at vcvars (vswhere off the PATH: the VS
  Installer folder added). The shader dump and glslang copied from banjo-nx's out/ (game-derived, not committed).
