# deko3d shader optimisations (uam)

banjo-nx, 2026-10-18, package 33. The game shaders on the deko3d back end are Renut's XenosRecomp SPIR-V, turned into
GLSL by SPIRV-Cross and `tools/deko3d_glsl.py`, and compiled by **uam** (devkitPro's deko3d compiler: Mesa's GLSL front
end, GLSL IR -> TGSI -> nouveau's nv50 IR codegen for Maxwell, SwitchWakerHD's patches) on the PC for the DKSH cache and
on the console for the variants it meets first. uam is not NIR/NAK: much of what NAK folds by itself it leaves in.
This document is the shader half of the GPU work (the copies, depth compression and Zcull are in
docs/deko3d-renderer.md section 13). docs/shader.md covers the NVK/NAK translator work of earlier packages.

## 1. Method

- **Where the time is:** package 32's console GPU profile split by pixel shader (`banjo_renut_diag_gpu_shaders`,
  logs32/renut_001.log, the gameplay windows: 2088 ms of frames). Heaviest: PS `2F36770769087F99` (282 ms of it),
  the depth-of-field composite `A5E26FB9039DA3BF` (120 ms, with its words copy), `C92BA80672FB9774`, `49A95BF514F078D7`,
  `0BBB240556B23871`, `2FA0FFBB905BFC82`, `3A25F8AD05B34870`.
- **What uam makes of them:** `tools/deko3d_shader_stats.py` builds every shader variant the pipeline lists use (the
  same text plume compiles: constants as `#define`s, 903-905 as render/pipeline.cpp and plume give them), compiles each
  with the PC uam, and reads the DKSH: instructions (without the scheduling words and the padding), GPRs, scratch, and
  the Maxwell opcodes by class (decoded with the opcode table of uam's own GM107 emitter). Per shader the mean over its
  used variants; a summary weighted by each pixel shader's share of the console GPU time (a vertex shader gets the time
  of the pixel shaders it is drawn with).
- **Reading uam's code:** `scratchpad shtool` decompiles a DKSH program with Ryujinx's shader translator (the
  disassembly as comments); that is how PRMT was checked (section 4).
- **Correctness:** every pass keeps the results bit for bit (the reasoning is with each item). Each optimised shader
  is compiled by uam in its default variant and with the alpha functions 7 and 8 (`deko3d_glsl_opt.py --check`; one
  that fails keeps its old text), every list variant compiles for the DKSH cache, and the picture is compared in
  Ryujinx against package 32 frame by frame.
- **Static, not executed, instructions:** a uniform branch the GPU skips costs little at run time however much code it
  skips. Items that only remove uniform branches are marked so; the console profile is the measurement.

Baseline (package 32, uam as it was): pixel shaders used by the lists 422, median 1059 instructions (p90 1270), GPRs
median 43 (max 54), weighted 878 instructions; vertex shaders 232, median 448 (p90 2153), GPRs median 32 (max 66),
weighted 1209 (int 636 of them: the byte swaps and address arithmetic of the vertex fetch).

## 2. The optimisations

Each has its pass in `tools/deko3d_glsl_opt.py` (GLSL, before uam), its patch in uam (`app/thirdparty/uam/mesa-imported/
codegen/nv50_ir_peephole.cpp`, `nv50_ir_emit_gm107.cpp`) or its run-time part, and a switch.

### 2.1 `scale1`: the texture resolution-scale divisors
Renut supports render scales (480p..4K, shadows at 4x, ambient occlusion at 2x); every texel-offset sample and texel
fetch divides the texture size by the texture word's scale code: a seven-way `?:` chain, `roundEven`, float and integer
conversions, per texture. deko3d draws at 1280x720 only: render/surfaces.cpp and graphics_options.cpp force 1x on
deko3d (a warning if the toml asks for another resolution), so the code is 0 and the chain goes. Measured: the
depth-of-field composite 426 -> 227 instructions alone; 106 pixel shaders have the chains.

### 2.2 `fragscale`: `gl_FragCoord / resolution_scale`
The Xenos pixel position input divides by `SharedConstants.resolution_scale` (1 on deko3d): a reciprocal and multiplies
in every shader that reads VPOS (depth of field, screen-space effects). Replaced by `1.0`, folded away.

### 2.3 `alpha` + `PipelineKey::alpha_func`: the alpha test per pipeline (constant 903)
Every one of the 3936 pixel shaders ended in the alpha test, its function read from `SharedConstants` at run time: a
seven-way chain that uam compiles to branches (SSY/BRA/SYNC), and a `discard`. A pixel shader that can discard keeps
Maxwell from testing depth before shading (the program header's kills-pixels bit): no draw had early depth test.
Now the draw's function is part of the pipeline key (`alpha_func` = function + 1; `banjo_deko3d_alpha_spec`) and the
shader's constant 903: with the test off (7) there is no compare and no discard, and the GPU rejects hidden pixels
before running the shader; with it on, one compare. Like the bool constants, a key not built yet draws with the generic
pipeline (903 = 0, the function read at run time) while a worker builds it. The pipeline list is version 4 (a version 3
list is converted at start, its keys generic). The translator's early-alpha block (a quad's alpha test before the
texture work, with derivatives) folds away with it for draws without the test. Measured: PS `571C4F7A46BF2EA6` (128
draws a frame) 86 -> 38 instructions, no KIL.

### 2.4 `alphamask`: the generic pipelines' alpha test as a bit test
The generic pipeline (until a draw's own is built, or with `banjo_deko3d_alpha_spec` off) keeps the run-time function;
its chain becomes `dk_alpha_pass(f, a, r)`: D3D's compare functions are bit masks (1 less, 2 equal, 4 greater), so
`(f & m) != 0` with m the comparison's bit; a NaN passes only `!=` (5) and always (7), as the chain did.

### 2.5 `tess`: the tessellation kind (constant 904)
Every vertex shader carries Renut's patch-grid code (the domain point from the vertex index, square roots, divisions,
four kinds) behind `SharedConstants.tessellation[0]`, 0 for every draw that is not tessellated. render/pipeline.cpp gives
constant 904 = 1 (kind 0) to every pipeline whose key is not a tessellated draw's (`per_instance_streams` 0;
`banjo_deko3d_tess_spec`): decided by the key, so no new pipelines. Mostly a uniform branch at run time (code size and
the registers the other path held). Skinned VS `8CFF0D8DCEB03B84`: 1043 -> 936.

### 2.6 `vsout`: the vertex shader writes only what the pixel shader reads (constant 905)
Every vertex shader wrote all 25 outputs (most of them zeros), so every vertex carried about 100 attribute words and
everything computing the unused ones ran. plume_deko3d gives a pipeline's vertex shader constant 905, the locations its
pixel shader declares as inputs of those the vertex shader writes (`Deko3DOptions::vsOutputs`,
`banjo_deko3d_vs_outputs`); each output write is under its bit, so the others, their arithmetic and the vertex fetches
behind them go. Not with a geometry shader (the rectangle-list and point-sprite ones read them all). An output read back
inside the shader is kept. `tools/deko3d_variant_cache.py`'s `stage_spec` gives the DKSH cache the same values.
Skinned VS: 936 -> 846 with its pixel shader (AST 26 -> 7).

### 2.7 Depth-only vertex shaders (905 = 0)
The same constant with no pixel shader (Renut's depth-only draws: the shadow maps, 318 draws a frame in package 32's
profile, about 2 ms): only `gl_Position` is computed. Skinned VS: 622 instructions and 44 GPRs (from 1043 and 59).

### 2.8 `pred`: a predicate known inside its own `if`
The translator's predicated code tests the same predicate again inside its own block (`if (_678) { ... if (_678) ...
} else { ... }`), with `bvec4(_678)` selects and phis that Mesa's GLSL optimiser does not see through. Inside
`if (X) { }` (X a bool the block does not assign) X is true; in the else block false. Heaviest PS, a real variant:
881 -> 774 instructions, 47 -> 43 GPRs (with the select pass).

### 2.9 `bool01`: a select of 1 and 0 is a conversion
`c ? 1.0 : 0.0` is `float(c)` (one FSET instead of a compare and two moves); the same for 0/1, unsigned, int, bool.

### 2.10 `sel`: `?:` as selects
uam turns a `?:` whose operands are not plain expressions (another `?:`, a vector) into an if/else: SSY, branches,
SYNC and moves. SPIRV-Cross's `?:` are SPIR-V `OpSelect`s, whose operands are computed either way. `dk_sel(c, a, b)`
(`mix(b, a, c)` per type, the GLSL 4.50 bool-selector form, a true select) compiles to ICMP/SEL. With the branches
gone, MemoryOpt also merges the duplicate loads of the same word (skinned VS: LDG 171 -> 57). Heaviest PS generic
variant 1672 -> 1423; across the lists PS weighted 878 -> 638 with 2.1-2.10 and the alpha function at 7.

### 2.11 uam: PRMT for byte moves (`PermuteOpt`, flag 1)
Xenos data is big-endian and the vertex fetch swaps each word it reads from memory with shifts and masks: the bytes of
each half (five instructions), the halves (three), both, or the four-term swap of the memory-export shaders. Maxwell's
PRMT puts any byte of a word, or zero, in each byte of the result. uam's GM107 emitter had no PRMT (added: register,
immediate, constant-buffer and RC forms, the field layout of the hardware's and Ryujinx's decoders); the pass turns
any OR that only moves whole bytes of one value (whole-byte shifts, byte masks, ORs) into one PRMT.

### 2.12 uam: a PRMT of a PRMT is one PRMT
The full swap is the half swap of the byte swap: the selectors compose (0x2301 then 0x1032 = 0x0123).

### 2.13 uam: a select of byte moves is one PRMT
The swap depends on the fetch constant's endian mode, the same for the whole draw: the shader computed all four forms
and chose with three selects per word. A select between two byte moves of one value is a PRMT whose selector is the
select of the two selectors; the selector selects are the same for every word of a fetch and LocalCSE (run again after
the pass) makes them one. Skinned VS 1660 -> 1043 (2.11-2.13 together), within 2 % of the shader compiled for one
known endian mode.

### 2.14 uam: the shift of a word index shared (`ShlAddDistribute`, flag 4)
`(i + k) << s` as `(i << s) + (k << s)` (the same 32-bit result, wrapping included), so the words `_m0[i]`,
`_m0[i + 1u]`, ... share `i << 2`. In the game's vertex shaders nouveau had already fused the shift into ISCADD, so it
changes little there (test shader: 33 -> 30).

### 2.15 Early depth test for draws without the alpha test (hardware, from 2.3)
Not an instruction count: with no discard and no depth output, Maxwell tests depth (and Zcull, now on) before the
pixel shader, so hidden pixels of opaque geometry are not shaded at all. Package 32's main pass was 34.8 ms opaque;
only the console measures this one.

## 2b. Package 34 (second round, 2026-10-18)

### 2.16 uam: storage-buffer offsets in the load (`GlobalOffsetFold`, flag 2)
Each word the vertex fetch reads from memory paid its own 64-bit address: base + ((i + k) << 2), an ISCADD and two
IADDs. The pass takes the constant k << 2 into the load's 24-bit offset (`ld [address + 4k]`), so the words of an
element share one address (LocalCSE). It handles the three forms the address arrives in (ADD, SHL of ADD, SHLADD) and
both operand orders of the 64-bit add (LoadPropagation puts a constant-buffer base second; the first version missed
that and never fired). MemoryOpt must not merge such loads or stores into 64/128-bit ones (the address is 4-byte
aligned only: a merged access would fault on the console): combineLd and combineSt refuse indirect global accesses
while it is on. Skinned VS 846 -> 774 (IADD 156 -> 108, ISCADD 56 -> 20).

### 2.17 `bonerow`: relative constant reads past the registers read zeros
The bone rows of skinning are `index < 256 ? c[min(index, 255)] : 0`: a select per component and a compare per row.
The float constant uploads now have a zero register 256 (kFloatConstantsUboBytes 4112, render/upload_ring.h; the
draw writes the 16 zero bytes), so `c[min(index, 256)]` is the same value. 364 reads in 108 vertex shaders; skinned VS
774 -> 759.

### 2.18 `predmul`: a predicated scale by one value
`mix(a, a * k, bvec3(p))` with k a broadcast scalar or a constant is `a * mix(vec3(1.0), k, bvec3(p))`: the three selects
are of the same two values (one select). Exact: uam's FMUL keeps denormals (FTZ only on one lowering), so a * 1.0 is a;
only a NaN's payload could change. 15,282 sites in the pixel shaders; heaviest PS 770 -> 765.

### 2.19 `vszero` / `pszero`: an input its vertex shader writes as zero (constant 906)
Renut's vertex shaders write every output, most as zeros; 480 of the 606 listed pipelines have a pixel shader that reads
one of those (the point coordinate, location 23, read by 2124 pixel shaders and written as zero except by the
point-sprite geometry shader). The vertex shader's GLSL records its zero outputs (`// banjo-vszero=0x...`); plume gives
the pipeline's pixel shader those of its inputs as constant 906 (not with a geometry shader); the pixel shader reads such
an input as `vecN(0.0)`: two interpolations fewer and what depended on them folds (DOF composite 145 -> 143).

### 2.20 No-op depth tests off (pipeline state)
A depth test of ALWAYS that writes nothing has no effect; render/pipeline.cpp turns it off, so the GPU does not read the
depth target for it (full-screen passes over a bound depth target).

### Bugs found by the review (all fixed for package 34)
- **The alpha function folding (2.3) did not reach the console in package 33.** 2.4's bit test was applied to every
  pipeline, and it hid the constant function from uam: a draw without the alpha test kept its discard and lost the early
  depth test. The bit test now only applies when 903 is 0 (`(g_banjoAlphaFunc != 0u) ? chain : dk_alpha_pass(...)`);
  function 8 compiles to no compare and no KIL again (PS 571C 54 -> 38).
- **Zcull after uploads, resolves and blits:** only image-to-image copies into a depth target invalidated Zcull;
  `copyTextureRegion` from a buffer, `resolveTexture(Region)` and the 2D-engine blit now do too.
- **`sel` and compound assignments:** `x += c ? a : b` was read as a condition `x += c`; `=` after `+-*/%&|^`, and `<<=`
  / `>>=`, are assignments (the uam check would have caught the result, but the shader would have lost the pass).
- **The DKSH cache tool read the alpha function at the wrong offset** (184, the reserved word; it is at 180, where a
  version 3 key had padding): no list's alpha functions reached the cache.
- **The uam thread's handle** was read by waiting threads without the lock (set under compileMutex now).

### Verification of the uam passes
`scratchpad uamx/difftest.py`: test vertex shaders (byte swaps of every form, zero bytes, shifts that are not byte moves,
two values, signed shifts, the endian select with every mode, the words of an element, the shift distribution) compiled
with UAM_BANJO_FLAGS 0 and 7, decompiled by Ryujinx's translator, run in Python on 300 random memory and constant sets
each, outputs compared bit for bit: all equal. A deliberate mismatch is caught.

## 2c. Package 34, third round: value numbering and branches (2026-10-18)

### 2.21 `sext`: a signed bitfield as one extract
The vertex fetch's signed fields (10:10:10:2 normals, signed shorts) arrive as `int(((x >> s) & m) << k) >> k`: a
shift, a mask and two shifts. With m n low bits and k = 32 - n it is `bitfieldExtract(int(x), s, n)`, one BFE (GLSL's
extract of an int sign-extends from bit n - 1, as the shift pair does). 548 sites in 108 vertex shaders, about one
instruction each (vs_03FC 405 -> 391).

### 2.22 uam: the fragment input interpolation cache (flag 8)
uam interpolated an input again at every read in another block (LocalCSE works within a block): 12 of the 40 IPAs of
the terrain shader 20CD. nv50_ir_from_tgsi.cpp keeps the interpolations of each structured scope (IF / ELSE / loop
bodies push one; ELSE starts a new one; subroutines and switches clear them): a read reuses an enclosing scope's,
which dominates it. Test shaders: IPA 24 -> 12, 34 -> 26, 15 -> 7. A value held across blocks can cost registers
(20CD 37 -> 42 GPRs), hence 2.23.

### 2.23 uam: the better of several codegens per pixel shader (flag 16, `DekoCompiler::CompileBest`)
A pixel shader is compiled with all the flags, then the codegen runs again from the same TGSI (the GLSL frontend once)
without 8, without 32 (2.24) and without both; the result kept has the fewest GPRs in steps of 8 (Maxwell allocates
registers per warp in units of 256: 8 a thread, which sets how many warps an SM holds), then the least code. The PC
tool (the DKSH cache) and the console's uam_api make the same choice.

### 2.24 uam: value numbering over the dominator tree (`DomCSE`, flags 32 and 128)
LocalCSE replaces within a block and GlobalCSE only pulls identical phi sources into a join: Renut's early alpha test
(a quad whose pixels all fail is discarded before the main body) computes the alpha with the main body's texture
fetches and arithmetic, and the main body computed them all again. The pass visits the blocks in dominator-tree
preorder; an instruction that computes what one of a dominating block (or an earlier one of its own) computes reuses
that result. Equivalent: the same value, or the same pure operation (loads only of constant buffers and inputs; no
flow, no predication) of equivalent sources, found by a structural hash and then checked. Interpolations are
equivalent but not replaced (flag 128): an interpolation again is one instruction, and a register held through the
alpha test raised 3A25 from 39 to 43 GPRs; what depends on them is still reused (and the second interpolations then
die). PS GPU-time weighted 607 -> 592 instructions (-2.4 %): the fur shader 2F36 (the top of the console profile)
753 -> 737, C92B / B3C1 / D40E 715 -> 684, 981A 126 -> 102; no shader went up a step of 8 GPRs (2.23 decides).

### 2.25 uam: a texture fetch reused whatever the write masks
The early alpha test fetches only the alpha (`texture(t, uv).w`, write mask 8) and the main body all four components
of the same fetch: the dominating fetch's mask is widened (a def added at its component's place) and the later fetch
goes. Test shader: three fetches of one texel (.w, .zx, .y) -> one TEXS .xyzw.

### 2.26 uam: equivalent if / else joins
Without loops (no back edge), a phi is replaced by one of a dominating join when the two diamonds branch on
equivalent conditions (the same compare of the same constant-buffer bits) and the values on each side are equivalent;
the side an edge comes from is the successor of the branch that dominates it, entered only from the branch, so nested
diamonds compare too. Test shaders: a repeated diamond 73 -> 44 statements, a nested one 85 -> 52. In the fur shader
it finds the alpha of the early test's texture-array branch; its colour (.xyz) is not computed there, so that fetch
stays (see 3).

### 2.27 uam: no reconvergence around uniform branches (`UniformJoins`, flag 256)
Every if / else got a reconvergence point (JOINAT / JOIN: SSY before the branch, SYNC at the join) unless nouveau
predicated it; nouveau even keeps uniform branches as branches (`isConstantCondition` lowers the predication limit to
4). A condition computed from constant buffers and immediates only (the shaders' feature bits, texture-array flags,
light and fog switches) is the same for every thread of a warp, which then cannot diverge: the pair goes. 49A9: SSY
21 -> 8, SYNC 42 -> 16, 928 -> 908 instructions; 2F36 1114 -> 1096; 3A25 979 -> 961.

### 2.28 `earlyhoist`: the main body's texture region ahead of the early alpha test (constant 907)
2.26 cannot take the fur shader's colour from the early test: the test computes only the alpha of its texture-array or
2D fetch. The pass moves the main body's first region (its statements up to its first `do { } while(false)`, pure, when
it fetches a texture the early test fetches and reads nothing the test assigns) ahead of the test; the region's
diamonds then dominate the test's, and 2.24-2.26 replace the test's copy with the region's results (were they not
equivalent, the region would only run earlier). Constant 907 (1; banjo_deko3d_early_hoist off gives 0) picks the place:
the region is written at both, its declarations ahead. Fur shader 903 = 5: 1096 -> 1058 instructions, 38 -> 34 texture
fetches, the same fetches as without the alpha test; 62 pixel shaders have the form, 13 in the lists (109 variants,
-4 % instructions, none worse). The cost: 2F36 and 2B31 (903 = 5) go from 40 to 42 GPRs, a step of 8; whether four
fetches fewer beat the warps lost is the console's to say (configs/banjo_deko3d_early_hoist_off.toml; the cache has both).

### 2.29 `ifmerge`: consecutive ifs on the same condition variable as one (2026-10-19)
SPIRV-Cross writes Renut's per-pixel branches as one if per value: the fur shader tests its shadow cascade `_678` in
three ifs in a row and `_914` in five. Each if on a per-pixel condition is a divergent branch: SSY, BRA, SYNC, and the
values that cross it held through each join. Two ifs at the same level on the same condition variable (or its
negation both times), with only declarations between them and nothing in the first assigning the variable, become one:
the first's then and the second's then, the first's else and the second's else (on each path the same statements in
the same order; the declarations go ahead). 2171 of the 4881 shaders have such ifs; every one compiles
(`--check`); `uamx/difftest_ifmerge.py` (repeated ifs with and without else, negated, mixed conditions that must not
merge, a discard inside) equal on 500 random input sets each. PS GPU-time weighted 586 -> 567 instructions (-3.3 %),
branch and join instructions 40 -> 23; the registers fall most: the heavy pixel shaders from 40 to 31-32 (one step of
8 fewer: up to 64 warps an SM instead of about 51), the fur shader 44 -> 39, the median 40 -> 32.

### 2.30 `dowhile`: `do { ... break; } while(false)` early exits as if / else (2026-10-19)
SPIRV-Cross writes the early exits of Renut's shaders (the texture-array choice, the early alpha test, the main body's
fog / colour paths) as a one-pass loop with breaks; Mesa does not unroll it, so uam compiles a loop (PBK, BRK, a
reconvergence for each exit) and 2.26's equivalent-if matching, which needs a function without loops, never ran in
those shaders. When the breaks are only the last statement of ifs at the loop's top level (no else) and its last
statement (a break inside a nested loop or switch is that loop's), it is `if (c) { A } else { rest }`, nested from the
last exit out, in a block of its own. All 7,355 such blocks of the pixel shaders convert; every shader compiles
(`--check`); `uamx/difftest_dowhile.py` (two exits, no final break, nested blocks with a discard, a break too deep to
convert) equal on 500 random input sets each. Fur shader 1038 -> 1024 (903 = 5) and 982 -> 975 (903 = 8), branch
and join instructions 59 -> 47 and 44 -> 37.

### 2.31 `selsame`: a select inside a select on the same condition (2026-10-19)
Renut's lights select each term on the light's flag twice, `dk_sel(c, (dk_sel(c, exp2(a), b)), d)`: with the same
condition text the inner select's other value can never be chosen, so it is `dk_sel(c, exp2(a), d)` (and
`dk_sel(c, a, dk_sel(c, x, y))` is `dk_sel(c, a, y)`). The conditions are variables or pure expressions (GLSL has no
calls with effects). The fur shader (903 = 5) 1024 -> about 1018 instructions. Every shader compiles; the cache of the
package (`deko3d_shaders_41.bin`, 9,188 variants, pruned to the pipelines of the lists, emulator run 54's list included) is made with this pass.

### The DKSH cache and the console's compiles
Emulator run 47 (package 34's round-two build) compiled 183 variants during play: 103 were alpha function 903 = 5
(greater), which the guesses for alpha-less list records lacked (8 and 7): `ALPHA_GUESS` is (8, 7, 5) and the cache is
made from the run's version-4 list as well (it records each pipeline's function). The cache tag is `uam110-sw8-bnx3`
(the compiler changed again); a cache of other flags (a toml key turning a uam pass off) is
`deko3d_shaders_f<flags>.bin` (PRMT off: f446), compiled by the console as it goes.

### Verification of the round
`uamx/difftest_ps.py <flags A> <flags B>`: 10 pixel shaders (branches, deep nesting, re-reads, discard, a repeated
diamond, a nested one, diamonds that must not merge (another bit, the inverse condition), a value before and after a
discard, swapped sides, uniform duplicates), 500 random input and constant-buffer sets each (constant words half small
bit fields, half floats), decompiled by Ryujinx's translator and run in Python: equal for 7/15, 15/175, 15/47,
175/431 and 7/447. `uamx/difftest.py 0 447` (the vertex suite): equal. The widened fetch checked on its decompile.

## 3. Measured and not taken

- **Texture fetches clustered early in their block** (uam flag 512, `TexCluster`, 2026-10-19): each fetch moved up to
  just after its sources so a block's fetches overlap. The game's fetches follow their coordinates' arithmetic: in the
  fur shader 21 fetch positions move by a few instructions at most, same registers, same size. Kept in uam, off.
- **Value numbering only for the costly operations** (flag 64: textures, loads, MUFU, conversions): never fewer GPRs
  than all of them with the interpolations kept (2.24), and more code. Kept as a flag, off.
- **The fur shader's texture-array branch:** the early alpha test computes only the alpha of the array fetch's two
  layers; reusing their colour needs the main body's code for .xyz moved into the early test's branches (a region
  fusion), not done yet.

- **Stacked-texture flag as a constant:** the 3D fetches test the texture word's stacked bit (a 2D array fetched as 3D)
  at run time. Folding it: heaviest PS 770 -> 715 static, but it is a uniform branch (a few instructions executed) and it
  would need a key field: not taken.
- **Maxwell scheduling:** the heaviest PS has most dependent ALU instructions at stall 6 (the full latency) and few
  dual issues; at its 43 GPRs the SM keeps about ten warps a scheduler, which hides that. A pre-RA scheduler is not worth
  it yet.
- **Folding the always-zero bone weights' adds** (`mix(a, b * w + a, w != 0)`): not exact when a bone row is infinite
  or NaN (b * 0 is NaN): kept.
- **Hoisting interpolated inputs:** uam already interpolates each input once per block (LocalCSE).
- **`clamp(x, 0.0, 1.0)`:** already free (FFMA.SAT).
- **Clamps to ±FLT_MAX after RCP/RSQ:** needed for the Xenos results (NaN and infinities); two FMNMX each stay.
- **The memory-export vertex shaders' scratch (10-12 KB a warp):** dynamic indexing of their packing arrays; rare
  draws, left.

## 4. The emulator and PRMT

Ryujinx's shader translator logged PRMT as not implemented and skipped it, so with 2.11-2.13 the characters (whose
bones are fetched from memory) vanished in the emulator. banjo-nx's Ryubing build now implements PRMT's default mode
(`src/Ryujinx.Graphics.Shader/Instructions/InstEmitPrmt.cs`: each result byte the selector nibble's byte of {C:A}, or
its sign). The decompiled test shader selects 0x2301 / 0x0123 / 0x1032 / 0x3210 for endian modes 1 / 2 / 3 / 0 as
intended. `banjo_deko3d_uam_prmt = false` (configs/banjo_deko3d_uam_prmt_off.toml) compiles without PRMT, with a DKSH
cache of its own (deko3d_shaders_f446.bin).

## 5. Running the tools

    python tools/deko3d_glsl_opt.py --in out/deko3d_shaders/glsl --out out/deko3d_shaders/glsl_opt --check
    python tools/deko3d_shader_package.py                  # glsl_opt -> app/out/renut_switch/deko3d_glsl.bin
    python tools/deko3d_variant_cache.py --list <lists> --out out/deko3d_shaders/deko3d_shaders.bin
    UAM_BANJO_FLAGS=446 python tools/deko3d_variant_cache.py --list <lists> --out .../deko3d_shaders_f446.bin
    python tools/deko3d_shader_stats.py --list <lists> --gpu-log logs32/renut_001.log [--passes a,b] [--base-glsl DIR]
        [--flags N --base-flags M]

The uam tool takes `UAM_BANJO_FLAGS` (default 447: 1 PRMT, 2 the offset fold, 4 the shift distribution, 8 the
interpolation cache, 16 the choice per pixel shader, 32 value numbering, 128 keeping interpolations, 256 no
reconvergence around uniform branches; 64 limits value numbering to the costly operations); the console's uam the same
through `Deko3DOptions::uamFlags` (`Deko3DUamFlags()`: banjo_deko3d_uam_prmt, banjo_deko3d_uam_gvn,
banjo_deko3d_uam_uniform_branches). The DKSH cache tag is `uam110-sw8-bnx3`.
