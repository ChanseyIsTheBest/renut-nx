#!/usr/bin/env python3
"""banjo-nx (2026-10-18, docs/deko3d-shader-optimisations.md): GLSL passes for uam, after tools/deko3d_glsl.py.

    python tools/deko3d_glsl_opt.py --in out/deko3d_shaders/glsl --out out/deko3d_shaders/glsl_opt [--passes a,b,...]
    python tools/deko3d_glsl_opt.py --list

uam is Mesa's GLSL compiler and nouveau's codegen (GLSL IR -> TGSI -> nv50 IR), not NIR: what NAK and the D3D
compilers fold for themselves it often leaves in. Each pass rewrites a form of SPIRV-Cross's output (of Renut's
XenosRecomp SPIR-V) into one that uam compiles to less code, with the same results. tools/deko3d_shader_stats.py
measures each (--passes there too). The passes and why are in PASSES below and in the document.

Only the game shaders (vs_/ps_<hash>.glsl) are rewritten; the text is what the console's uam compiles, so the
DKSH cache (tools/deko3d_variant_cache.py --glsl <out>) and the package (tools/deko3d_shader_package.py --glsl <out>)
take the output directory. Game data: the build tree only.
"""
import argparse
import os
import re
import sys

# ---------------------------------------------------------------------------------------------------------------------
# helpers


def matching(text, open_index, open_ch, close_ch):
    depth = 0
    for i in range(open_index, len(text)):
        c = text[i]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i
    return -1


def main_span(text):
    """(start, end) of main()'s body, or None."""
    m = re.search(r'\nvoid main\(\)\n\{', text)
    if not m:
        return None
    open_i = m.end() - 1
    close_i = matching(text, open_i, '{', '}')
    if close_i < 0:
        return None
    return open_i + 1, close_i


def insert_before_main(text, code):
    at = text.find('\nvoid main()')
    if at < 0:
        return text
    return text[:at + 1] + code + text[at + 1:]


def insert_globals(text, code):
    """After the #version line and the banjo-dk / extension / define lines that follow it."""
    lines = text.split('\n')
    k = 1
    while k < len(lines) and (lines[k].startswith('//') or lines[k].startswith('#extension') or
                              lines[k].startswith('#define dkh')):
        k += 1
    lines[k:k] = code.rstrip('\n').split('\n')
    return '\n'.join(lines)


def map_main(text, fn):
    span = main_span(text)
    if not span:
        return text
    a, b = span
    return text[:a] + fn(text[a:b]) + text[b:]


# ---------------------------------------------------------------------------------------------------------------------
# pass: sel — `c ? a : b` -> dk_sel(c, a, b)

SEL_TYPES = [('float', 'mix(b, a, c)'), ('vec2', 'mix(b, a, bvec2(c))'), ('vec3', 'mix(b, a, bvec3(c))'),
             ('vec4', 'mix(b, a, bvec4(c))'), ('int', 'mix(b, a, c)'), ('ivec2', 'mix(b, a, bvec2(c))'),
             ('ivec3', 'mix(b, a, bvec3(c))'), ('ivec4', 'mix(b, a, bvec4(c))'), ('uint', 'mix(b, a, c)'),
             ('uvec2', 'mix(b, a, bvec2(c))'), ('uvec3', 'mix(b, a, bvec3(c))'), ('uvec4', 'mix(b, a, bvec4(c))'),
             ('bool', 'mix(b, a, c)'), ('bvec2', 'mix(b, a, bvec2(c))'), ('bvec3', 'mix(b, a, bvec3(c))'),
             ('bvec4', 'mix(b, a, bvec4(c))')]
SEL_HELPERS = ''.join(f'{t} dk_sel(bool c, {t} a, {t} b) {{ return {body}; }}\n' for t, body in SEL_TYPES)

def _is_assignment(s, i):
    """The '=' at i is an assignment (=, +=, <<=, ...), not part of ==, !=, <= or >=."""
    if i + 1 < len(s) and s[i + 1] == '=':
        return False  # the first of '=='
    prev = s[i - 1] if i > 0 else ''
    if prev in ('=', '!'):
        return False  # '==' or '!='
    if prev in ('<', '>'):
        return i > 1 and s[i - 2] == prev  # '<<=' / '>>=' assign, '<=' / '>=' compare
    return True  # '=' or a compound assignment


def _ternary_at(s, q):
    """For the '?' at q: (start, colon, end) of the conditional expression, or None."""
    # condition: back to the enclosing bracket, a comma, an assignment, a ternary operator, or the statement start
    depth = 0
    i = q - 1
    while i >= 0:
        c = s[i]
        if c in ')]':
            depth += 1
        elif c in '([':
            if depth == 0:
                break
            depth -= 1
        elif depth == 0:
            if c in ',;{}?:':
                break
            if c == '=' and _is_assignment(s, i):
                break
        i -= 1
    start = i + 1
    if re.match(r'\s*return\b', s[start:q]):
        start = start + s[start:q].index('return') + len('return')
    # then-part: to the ':' that pairs with this '?'
    depth = 0
    pending = 0
    j = q + 1
    colon = -1
    while j < len(s):
        c = s[j]
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
            if depth < 0:
                return None
        elif depth == 0:
            if c == '?':
                pending += 1
            elif c == ':':
                if pending == 0:
                    colon = j
                    break
                pending -= 1
            elif c in ',;':
                return None
        j += 1
    if colon < 0:
        return None
    # else-part: to the enclosing bracket's close, a comma, ';', or a ':' of an enclosing conditional
    depth = 0
    pending = 0
    k = colon + 1
    while k < len(s):
        c = s[k]
        if c in '([':
            depth += 1
        elif c in ')]':
            if depth == 0:
                break
            depth -= 1
        elif depth == 0:
            if c in ',;':
                break
            if c == '?':
                pending += 1
            elif c == ':':
                if pending == 0:
                    break
                pending -= 1
        k += 1
    return start, colon, k


def pass_sel(text):
    """Every conditional expression a select: uam turns a ?: whose operands are not plain expressions (another ?:, a
    vector) into an if/else (SSY, branches, SYNC and moves), and a mix() with a bool selector into SEL/ICMP. The two
    operands of SPIRV-Cross's ?: are values SPIR-V's OpSelect computed both of anyway."""
    count = 0

    def body(s):
        nonlocal count
        lines = s.split('\n')
        for n, line in enumerate(lines):
            if '?' not in line or line.lstrip().startswith('#') or line.lstrip().startswith('//'):
                continue
            guard = 0
            while '?' in line and guard < 400:
                guard += 1
                done = False
                # the innermost first: a '?' whose three parts hold no other '?'
                for q in [m.start() for m in re.finditer(r'\?', line)]:
                    t = _ternary_at(line, q)
                    if t is None:
                        continue
                    start, colon, end = t
                    cond, a, b = line[start:q], line[q + 1:colon], line[colon + 1:end]
                    if '?' in cond or '?' in a or '?' in b:
                        continue
                    lead = cond[:len(cond) - len(cond.lstrip())]
                    trail = b[len(b.rstrip()):]
                    line = (line[:start] + lead + f'dk_sel({cond.strip()}, {a.strip()}, {b.strip()})' + trail +
                            line[end:])
                    count += 1
                    done = True
                    break
                if not done:
                    break
            lines[n] = line
        return '\n'.join(lines)

    out = map_main(text, body)
    if count:
        out = insert_before_main(out, SEL_HELPERS)
    return out


# ---------------------------------------------------------------------------------------------------------------------
# pass: alpha — the alpha test function as specialization constant 903

ALPHA_WORD = 'g_UboShared.v[34u].w'
ALPHA_DECL = ('#ifndef SPIRV_CROSS_CONSTANT_ID_903\n#define SPIRV_CROSS_CONSTANT_ID_903 0u\n#endif\n'
              'const uint g_banjoAlphaFunc = SPIRV_CROSS_CONSTANT_ID_903;\n'
              '#define DK_ALPHA_FUNC ((g_banjoAlphaFunc != 0u) ? (g_banjoAlphaFunc - 1u) : g_UboShared.v[34u].w)\n')


def pass_alpha(text):
    """The alpha test's compare function (SharedConstants.alpha_test_func) as specialization constant 903, its value
    + 1 (0: read at run time): the pipeline key has it (render/pipeline.cpp), so a draw without the alpha test (7,
    always) has no compare chain and no discard, and a pixel shader without discard and depth output keeps the GPU's
    early depth test (the SPH's kills_pixels bit is off). The early-alpha block (translator: the quad's alpha test
    before the texture fetches) folds away with it."""
    if ALPHA_WORD not in text:
        return text
    span = main_span(text)
    if not span:
        return text
    a, b = span
    body = text[a:b].replace(ALPHA_WORD, 'DK_ALPHA_FUNC')
    text = text[:a] + body + text[b:]
    return insert_before_main(text, ALPHA_DECL)


# ---------------------------------------------------------------------------------------------------------------------
# pass: scale1 — every texture and target at the console's resolution

SCALE_CHAIN_RE = re.compile(
    r'\((\w+) == 1u\) \? 0\.666666686534881591796875 : \(\(\1 == 2u\) \? 1\.5 : \(\(\1 == 3u\) \? 2\.0 : '
    r'\(\(\1 == 4u\) \? 3\.0 : \(\(\1 == 5u\) \? 4\.0 : \(\(\1 == 6u\) \? 6\.0 : '
    r'\(\(\1 == 7u\) \? 1\.33333337306976318359375 : 1\.0\)\)\)\)\)\)')
SCALE_CODE_RE = re.compile(r'^(\s*uint (_\d+) = )\(([^;]*?) >> 26u\) & 7u;$', re.M)
ROUND_RE = re.compile(r'roundEven\((vec[234])\((uvec[234])\(textureSize\(([\w]+, \d+)\)\)\) / vec[234]\(1\.0\)\)')
ROUNDTRIP_RE = re.compile(r'(uvec[234])\((vec[234])\(\1\(textureSize\(([\w]+, \d+)\)\)\)\)')
SCALE_WORD = 'uintBitsToFloat(g_UboShared.v[83u].z)'


def pass_scale1(text):
    """The deko3d renderer draws every texture at the console's scale (render/surfaces.cpp and graphics_options.cpp
    force 1x on deko3d): a texture word's scale code (bits 26-28) is 0. The per-texture scale divisor (a seven-way
    select on the code, before every sample with a texel offset and every texel-coordinate fetch) and the roundEven of
    size / 1 with its float and integer conversions go."""
    text = SCALE_CODE_RE.sub(lambda m: f'{m.group(1)}0u;', text)
    text = SCALE_CHAIN_RE.sub('1.0', text)
    text = ROUND_RE.sub(lambda m: f'{m.group(1)}({m.group(2)}(textureSize({m.group(3)})))', text)
    text = ROUNDTRIP_RE.sub(lambda m: f'{m.group(1)}(textureSize({m.group(3)}))', text)
    return text


def pass_fragscale(text):
    """SharedConstants.resolution_scale is 1 on deko3d (1280x720 targets only): the gl_FragCoord / resolution_scale
    of every shader that reads the pixel position (the Xenos VPOS input: the depth of field, the screen-space
    shadows and effects) is gl_FragCoord, without its reciprocal and multiplies."""
    span = main_span(text)
    if span:
        a, b = span
        text = text[:a] + text[a:b].replace(SCALE_WORD, '1.0') + text[b:]
    return text


# ---------------------------------------------------------------------------------------------------------------------
# pass: alphamask — the generic alpha test as a bit test

ALPHA_HEAD = '!(dk_sel((DK_ALPHA_FUNC == 0u), false, (dk_sel((DK_ALPHA_FUNC == 1u), ('
ALPHA_CMP_RE = re.compile(r'^\(dk_sel\(\(DK_ALPHA_FUNC == 0u\), false, \(dk_sel\(\(DK_ALPHA_FUNC == 1u\), \((\S+) < (\S+)\), '
                          r'\(dk_sel\(\(DK_ALPHA_FUNC == 2u\), \(\1 == \2\), \(dk_sel\(\(DK_ALPHA_FUNC == 3u\), \(\1 <= \2\), '
                          r'\(dk_sel\(\(DK_ALPHA_FUNC == 4u\), \(\1 > \2\), \(dk_sel\(\(DK_ALPHA_FUNC == 5u\), \(\1 != \2\), '
                          r'\(dk_sel\(\(DK_ALPHA_FUNC == 6u\), \(\1 >= \2\), true\)\)[)]*$')
ALPHA_HELPER = ('bool dk_alpha_pass(uint f, float a, float r)\n{\n'
                '    // D3D compare functions are a mask: 1 less, 2 equal, 4 greater (0 never, 7 always); with a NaN only\n'
                '    // not-equal (5) and always (7) pass.\n'
                '    uint m = mix(mix(mix(5u, 2u, a == r), 4u, a > r), 1u, a < r);\n'
                '    return mix((f & m) != 0u, (f & 5u) == 5u, m == 5u);\n}\n')


def pass_alphamask(text):
    """The alpha test of a pipeline whose function is read at run time (constant 903 at 0: the generic pipeline a draw
    uses while its own is built, or banjo_deko3d_alpha_spec off): the seven-way compare chain (seven compares and
    selects) as the compare function's bit mask (D3D's functions are less | equal | greater bits). Runs after sel."""
    out = []
    pos = 0
    n = 0
    while True:
        at = text.find(ALPHA_HEAD, pos)
        if at < 0:
            out.append(text[pos:])
            break
        close = matching(text, at + 1, '(', ')')
        inner = text[at + 1:close + 1]  # the whole parenthesised chain after the '!'
        m = ALPHA_CMP_RE.match(inner)
        if close < 0 or not m:
            out.append(text[pos:at + 1])
            pos = at + 1
            continue
        out.append(text[pos:at])
        # Only where the function is read at run time: with constant 903 the chain folds to one compare (or to
        # nothing, and no discard, for 7), which the bit test would hide from uam (package 33 kept the discard).
        out.append(f'!((g_banjoAlphaFunc != 0u) ? {inner} : '
                   f'dk_alpha_pass(g_UboShared.v[34u].w, {m.group(1)}, {m.group(2)}))')
        pos = close + 1
        n += 1
    if not n:
        return text
    return insert_before_main(''.join(out), ALPHA_HELPER)


# ---------------------------------------------------------------------------------------------------------------------
# pass: pred — a condition known inside its own if

def pass_pred(text):
    """Inside `if (X) { ... }` (X a bool the block does not assign), an inner `if (X)` is `if (true)` and `if (!X)`
    `if (false)`, X anywhere else in the block is true (a bvec4(X) of a mix, a ?:), and the same in the else block
    the other way round. In the translator's predicated blocks the same predicate is tested again and again (the inner
    else blocks, their copies and the phis are dead), which Mesa's GLSL optimiser does not see."""
    span = main_span(text)
    if not span:
        return text
    a, b = span
    lines = text[a:b].split('\n')
    bools = set(re.findall(r'^\s*bool (_\d+)\b', text[a:b], re.M))
    if not bools:
        return text
    # the line of each block's closing brace, and the lines assigning each bool
    close_of = {}
    opened = []
    for n, line in enumerate(lines):
        st = line.strip()
        if st == '{':
            opened.append(n)
        elif (st == '}' or st.startswith('} while')) and opened:
            close_of[opened.pop()] = n
    assign_at = {}
    for n, line in enumerate(lines):
        for m in re.finditer(r'(?<![\w.])(_\d+)\s*=[^=]', line):
            if m.group(1) in bools:
                assign_at.setdefault(m.group(1), []).append(n)

    def stable(var, open_line):
        close = close_of.get(open_line, len(lines))
        return not any(open_line < k < close for k in assign_at.get(var, ()))

    stack = []      # (indent of the block's braces, var or None, value inside)
    pending = None  # (indent, var, value) of an if or else whose '{' comes next
    last_if = {}    # indent -> (var, value) of the if just closed (for its else)
    changed = 0
    for n, line in enumerate(lines):
        stripped = line.strip()
        indent = len(line) - len(line.lstrip())
        known = {var: val for _, var, val in stack if var is not None}
        m = re.match(r'^(\s*)(?:else )?if \((!?)(_\d+)\)$', line)
        if m and m.group(3) in known:
            val = known[m.group(3)] != (m.group(2) == '!')
            lines[n] = f'{m.group(1)}{"else " if stripped.startswith("else") else ""}if ({"true" if val else "false"})'
            changed += 1
        if m and m.group(3) in bools:
            pending = (indent, m.group(3), m.group(2) != '!')
            continue
        if stripped == 'else' and indent in last_if:
            var, val = last_if.pop(indent)
            pending = (indent, var, not val)
            continue
        if stripped == '{':
            if pending and pending[0] == indent and stable(pending[1], n):
                stack.append(pending)
            else:
                stack.append((indent, None, None))
            pending = None
            continue
        pending = None
        if stripped == '}' or stripped.startswith('} while'):
            if stack:
                ind, var, val = stack.pop()
                if var is not None and stripped == '}':
                    last_if[ind] = (var, val)
            continue
        last_if = {k: v for k, v in last_if.items() if k < indent}
        if known:
            def use(mm):
                v = mm.group(1)
                if v in known:
                    return 'true' if known[v] else 'false'
                return mm.group(0)
            new = re.sub(r'(?<![\w.])(_\d+)(?![\w\[])(?!\s*=[^=])', use, line)
            if new != line:
                lines[n] = new
                changed += 1
    if not changed:
        return text
    return text[:a] + '\n'.join(lines) + text[b:]


# ---------------------------------------------------------------------------------------------------------------------

# ---------------------------------------------------------------------------------------------------------------------
# pass: tess — the vertex shader's tessellation kind as specialization constant 904

TESS_WORD = 'g_UboShared.v[65u].x'
TESS_DECL = ('#ifndef SPIRV_CROSS_CONSTANT_ID_904\n#define SPIRV_CROSS_CONSTANT_ID_904 0u\n#endif\n'
             'const uint g_banjoTessKind = SPIRV_CROSS_CONSTANT_ID_904;\n'
             '#define DK_TESS_KIND ((g_banjoTessKind != 0u) ? (g_banjoTessKind - 1u) : g_UboShared.v[65u].x)\n')


def pass_tess(text):
    """SharedConstants.tessellation[0] (the patch kind: 0 for a draw that is not tessellated) as specialization
    constant 904, its value + 1 (0: read at run time): render/pipeline.cpp gives 1 to every pipeline whose key is not a
    tessellated draw's (per_instance_streams 0), so the vertex shader's patch-grid code (the domain point from the
    vertex index: square roots, divisions, the four kinds' branches) goes, and its registers with it. No new
    pipelines: the key decides it."""
    if TESS_WORD not in text:
        return text
    span = main_span(text)
    if not span:
        return text
    a, b = span
    text = text[:a] + text[a:b].replace(TESS_WORD, 'DK_TESS_KIND') + text[b:]
    return insert_before_main(text, TESS_DECL)


# ---------------------------------------------------------------------------------------------------------------------
# pass: vsout — the vertex shader's outputs the pixel shader reads, as specialization constant 905

VSOUT_DECL = ('#ifndef SPIRV_CROSS_CONSTANT_ID_905\n#define SPIRV_CROSS_CONSTANT_ID_905 4294967295u\n#endif\n'
              'const uint g_banjoVsOutputs = SPIRV_CROSS_CONSTANT_ID_905;\n')
VSOUT_RE = re.compile(r'^layout\(location = (\d+)\) out (\w+) (\w+);$', re.M)


def pass_vsout(text):
    """Each write of a vertex shader output (layout location N) under `(g_banjoVsOutputs & (1u << N)) != 0u`,
    specialization constant 905: plume_deko3d gives a pipeline's vertex shader the locations its pixel shader reads (none
    for a depth-only pipeline: the shadow maps), so the outputs nothing reads, the arithmetic and the vertex fetches
    behind them go. An output the shader reads back is left alone."""
    outs = [(int(m.group(1)), m.group(3)) for m in VSOUT_RE.finditer(text) if int(m.group(1)) < 32]
    if not outs:
        return text
    span = main_span(text)
    if not span:
        return text
    a, b = span
    body = text[a:b]
    wrapped = 0
    for loc, name in outs:
        uses = list(re.finditer(r'(?<![\w.])' + re.escape(name) + r'\b', body))
        writes = re.findall(r'^\s*' + re.escape(name) + r'(?:\.[xyzw]+)?\s*=[^=][^;]*;$', body, re.M)
        if not writes or len(writes) != len(uses):
            continue  # read back somewhere, or written in a form not handled: kept
        body = re.sub(r'^(\s*)(' + re.escape(name) + r'(?:\.[xyzw]+)?\s*=[^=][^;]*;)$',
                      lambda m: f'{m.group(1)}if ((g_banjoVsOutputs & {1 << loc}u) != 0u) {{ {m.group(2)} }}',
                      body, flags=re.M)
        wrapped += 1
    if not wrapped:
        return text
    text = text[:a] + body + text[b:]
    return insert_before_main(text, VSOUT_DECL)


# ---------------------------------------------------------------------------------------------------------------------
# passes: vszero / pszero — an input its vertex shader writes as zero (constant 906)

ZERO_RE = re.compile(r'^(?:(?:vec[234])\()?0\.0\)?$')
PSIN_RE = re.compile(r'^layout\(location = (\d+)\) in (\w+) (\w+);$', re.M)
PSZERO_DECL = ('#ifndef SPIRV_CROSS_CONSTANT_ID_906\n#define SPIRV_CROSS_CONSTANT_ID_906 0u\n#endif\n'
               'const uint g_banjoPsZeroIn = SPIRV_CROSS_CONSTANT_ID_906;\n')


def pass_vszero(text):
    """Records in a `// banjo-vszero=0x...` line the outputs the vertex shader writes as zero (one write of 0.0 or
    vecN(0.0), nothing else touching it): plume_deko3d gives the pipeline's pixel shader those of its inputs as constant
    906 (pszero). Renut's vertex shaders write every output; the point coordinate (location 23) is zero except under
    the point-sprite geometry shader, and 2124 pixel shaders read it."""
    span = main_span(text)
    if not span:
        return text
    body = text[span[0]:span[1]]
    mask = 0
    for m in VSOUT_RE.finditer(text):
        loc, name = int(m.group(1)), m.group(3)
        if loc >= 32:
            continue
        uses = re.findall(r'(?<![\w.])' + re.escape(name) + r'\b', body)
        writes = re.findall(r'^\s*' + re.escape(name) + r' = ([^;]*);$', body, re.M)
        if len(uses) == 1 and len(writes) == 1 and ZERO_RE.match(writes[0].strip()):
            mask |= 1 << loc
    if not mask:
        return text
    lines = text.split('\n')
    lines.insert(1, f'// banjo-vszero=0x{mask:x}')
    return '\n'.join(lines)


def pass_pszero(text):
    """Each input read through `DK_IN_<name>`: `vecN(0.0)` when bit <location> of constant 906 is set (its vertex
    shader writes it as zero, vszero), else the input. The interpolation and what depends on it go."""
    ins = [(int(m.group(1)), m.group(2), m.group(3)) for m in PSIN_RE.finditer(text) if int(m.group(1)) < 32]
    if not ins:
        return text
    span = main_span(text)
    if not span:
        return text
    a, b = span
    body = text[a:b]
    macros = ''
    for loc, typ, name in ins:
        if not re.search(r'(?<![\w.])' + re.escape(name) + r'\b', body):
            continue
        body = re.sub(r'(?<![\w.])' + re.escape(name) + r'\b', f'DK_IN_{name}', body)
        macros += (f'#define DK_IN_{name} (((g_banjoPsZeroIn & {1 << loc}u) != 0u) ? {typ}(0.0) : {name})\n')
    if not macros:
        return text
    text = text[:a] + body + text[b:]
    return insert_before_main(text, PSZERO_DECL + macros)


# ---------------------------------------------------------------------------------------------------------------------
# pass: bonerow — a relative constant read past the registers reads the zero register

BONEROW_RE = re.compile(r'mix\(vec4\(0\.0\), (g_Ubo(?:Vertex|Pixel))\.v\[min\((_\d+), 255u\)\], bvec4\(\2 < 256u\)\)')


def pass_bonerow(text):
    """Renut's relative constant reads (the bone rows of skinning: c[a0 + n]) are `index < 256 ? c[min(index, 255)] :
    0`, a select per component and a compare. The float constant uploads have a zero register 256 after the 256
    registers (render/upload_ring.h, kFloatConstantsUboBytes), so `c[min(index, 256)]` is the same value with no
    select: an index past the registers reads zeros."""
    out, n = BONEROW_RE.subn(lambda m: f'{m.group(1)}.v[min({m.group(2)}, 256u)]', text)
    if not n:
        return text
    for block in set(m.group(1) for m in BONEROW_RE.finditer(text)):
        out = re.sub(r'(uniform type_ConstantBuffer_RenutUboFloat\n\{\n\s*vec4 v\[)256(\];\n\} ' + block + ';)',
                     r'\g<1>257\2', out)
    return out


# ---------------------------------------------------------------------------------------------------------------------
# pass: predmul — a predicated scale by one value

PREDMUL_RE = re.compile(r'mix\((_\d+(?:\.[xyzw]+)?), \1 \* (_\d+\.([xyzw])\3{1,3}|vec[234]\([0-9.e+-]+\)), '
                        r'bvec([234])\((_\d+)\)\)')


def pass_predmul(text):
    """The translator's predicated vector multiply by one scalar (a broadcast swizzle or a constant):
    `mix(a, a * k, bvec3(p))` (three multiplies and three selects) is `a * mix(vec3(1.0), k, bvec3(p))`: the selects
    are of the same two values, one select. Exact: a * 1.0 is a (uam's multiplies keep denormals; only a NaN's payload
    may change)."""
    def rep(m):
        n = m.group(4)
        return f'({m.group(1)} * mix(vec{n}(1.0), {m.group(2)}, bvec{n}({m.group(5)})))'
    return map_main(text, lambda body: PREDMUL_RE.sub(rep, body))


# ---------------------------------------------------------------------------------------------------------------------
# pass: sext — a signed bitfield as one extract

SEXT_RE = re.compile(r'int\(\(\((_\d+) >> (\d+)u\) & (\d+)u\) << (\d+)u\) >> (\d+)')


def pass_sext(text):
    """The vertex fetch's signed fields (10:10:10:2 normals, signed shorts): `int(((x >> s) & m) << k) >> k`, a shift,
    a mask, a shift and an arithmetic shift, is `bitfieldExtract(int(x), s, n)` (one BFE; GLSL's extract of an int
    sign-extends from bit n - 1, as the shift pair does). Only when m is n low bits and k = 32 - n."""
    def rep(m):
        x, sh, mask, k1, k2 = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))
        n = mask.bit_length()
        if mask != (1 << n) - 1 or k1 != k2 or k1 != 32 - n or sh + n > 32 or n == 0:
            return m.group(0)
        return f'bitfieldExtract(int({x}), {sh}, {n})'
    return map_main(text, lambda body: SEXT_RE.sub(rep, body))


# ---------------------------------------------------------------------------------------------------------------------
# pass: bool01 — a select of the two constants of a type is a conversion

BOOL01 = {('1.0', '0.0'): 'float({c})', ('0.0', '1.0'): 'float(!({c}))', ('1u', '0u'): 'uint({c})',
          ('0u', '1u'): 'uint(!({c}))', ('1', '0'): 'int({c})', ('0', '1'): 'int(!({c}))',
          ('true', 'false'): '({c})', ('false', 'true'): '(!({c}))'}


def pass_bool01(text):
    """`c ? 1.0 : 0.0` is float(c) (one FSET; the select is a compare and two moves), and the same for 0.0 / 1.0, the
    integers and the bools: the translator's predicates turned into numbers (setp results, the Xenos kill and pred
    instructions) are written so."""
    count = 0

    def body(s):
        nonlocal count
        lines = s.split('\n')
        for n, line in enumerate(lines):
            if '?' not in line:
                continue
            guard = 0
            while guard < 200:
                guard += 1
                done = False
                for q in [m.start() for m in re.finditer(r'\?', line)]:
                    t = _ternary_at(line, q)
                    if t is None:
                        continue
                    start, colon, end = t
                    cond, x, y = line[start:q], line[q + 1:colon].strip(), line[colon + 1:end].strip()
                    form = BOOL01.get((x, y))
                    if form is None or '?' in cond:
                        continue
                    lead = cond[:len(cond) - len(cond.lstrip())]
                    trail = line[colon + 1:end][len(line[colon + 1:end].rstrip()):]
                    line = line[:start] + lead + form.format(c=cond.strip()) + trail + line[end:]
                    count += 1
                    done = True
                    break
                if not done:
                    break
            lines[n] = line
        return '\n'.join(lines)

    return map_main(text, body)


# ---------------------------------------------------------------------------------------------------------------------
# pass: earlyhoist — the main body's first texture region ahead of Renut's early alpha test

EARLY_IF_RE = re.compile(r'\n([ \t]*)if \(DK_ALPHA_FUNC != 7u\)\n[ \t]*\{')
ASSIGNED_RE = re.compile(r'\b(_\d+)(?:\.[xyzw]+)?\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)')


def _next_statement(body, at):
    """(start, end) of the statement at 'at' (after blanks): a simple one up to its ';', or a `do { ... } while(false);`;
    None for anything else (if, for, a block, the end of the enclosing block)."""
    n = len(body)
    i = at
    while i < n and body[i] in ' \t\n':
        i += 1
    if i >= n or body[i] == '}':
        return None
    if re.match(r'do\s*\{', body[i:i + 8]):
        open_i = body.index('{', i)
        close_i = matching(body, open_i, '{', '}')
        if close_i < 0:
            return None
        m = re.match(r'\s*while\s*\(\s*false\s*\)\s*;', body[close_i + 1:])
        if not m:
            return None
        return i, close_i + 1 + m.end()
    if re.match(r'(if|for|while|switch|return|discard|break|continue|else)\b', body[i:]) or body[i] == '{':
        return None
    depth = 0
    for j in range(i, n):
        c = body[j]
        if c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
            if depth < 0:
                return None
        elif c == ';' and depth == 0:
            return i, j + 1
        elif c == '{':
            return None
    return None


def pass_earlyhoist(text):
    """Renut's early alpha test (a quad whose pixels all fail is discarded before the main body) computes the alpha as
    the main body does, and the main body's first region then fetches the same textures through the same uniform
    branches (the fur shader: a texture array or a 2D texture by a constant-buffer bit). uam's value numbering
    replaces what an equivalent dominating computation already did, so the main body's region goes first: its
    statements up to its first `do { } while(false)` (pure: no discard, no output, nothing the early test assigns), when
    that region fetches a texture the early test also fetches. uam then finds the early test's alpha in the region's
    results and drops the early copy; were the two not equivalent, the region would only run earlier (pure code).
    Specialization constant 907 (1 by default; banjo_deko3d_early_hoist off gives 0) picks the place: the region is
    written at both, its top-level declarations ahead of the first."""
    changed = []

    def body(b):
        m = EARLY_IF_RE.search(b)
        if not m:
            return b
        open_i = b.index('{', m.end() - 1)
        close_i = matching(b, open_i, '{', '}')
        if close_i < 0:
            return b
        early = b[m.start() + 1:close_i + 1]
        stmts = []
        at = close_i + 1
        while True:
            st = _next_statement(b, at)
            if not st:
                return b
            stmts.append(st)
            at = st[1]
            if b[st[0]:st[0] + 2] == 'do':
                break
            if len(stmts) > 12:
                return b
        start, end = stmts[0][0], stmts[-1][1]
        region = b[start:end]
        if re.search(r'\b(discard|return)\b|\bout_var_|gl_FragDepth|\bdFd[xy]|\bfwidth', region):
            return b
        # nothing the early test assigns (declared before it) may be read by the region
        declared_in_early = set(re.findall(r'\b(?:float|int|uint|bool|[biu]?vec[234]|mat[234])\s+(_\d+)\b', early))
        assigned = {v for v in ASSIGNED_RE.findall(early)} - declared_in_early
        if assigned & set(re.findall(r'\b_\d+\b', region)):
            return b
        # worth it only when the region fetches what the early test fetches
        tex_region = set(re.findall(r'\btexture\w*\((dk_tex_b\d+)', region))
        tex_early = set(re.findall(r'\btexture\w*\((dk_tex_b\d+)', early))
        if not (tex_region & tex_early):
            return b
        indent = m.group(1)
        # the region's top-level declarations go ahead of both places; in each it assigns
        decls, inner = [], []
        for a0, a1 in stmts:
            st = b[a0:a1]
            d = re.match(r'((?:float|int|uint|bool|[biu]?vec[234]|mat[234])\s+)(_\d+)\s*(=|;)', st)
            if not d and re.match(r'(const\s+)?(float|int|uint|bool|[biu]?vec[234]|mat[234]|[biu]?vec\d|\w+\s+_\d+\s*\[)\b',
                                  st):
                return b  # a declaration this does not split (const, array): left where it is
            if d:
                decls.append(f'{d.group(1)}{d.group(2)};')
                st = '' if d.group(3) == ';' else st[len(d.group(1)):]
            if st:
                inner.append(st)
        inner_text = ('\n' + indent + '    ').join(x.replace('\n', '\n    ') for x in inner)

        def place(cond):
            return f'{indent}if (g_banjoEarlyHoist {cond} 0u)\n{indent}{{\n{indent}    {inner_text}\n{indent}}}'

        changed.append(True)
        head = ''.join(f'{indent}{d}\n' for d in decls)
        return (b[:m.start() + 1] + head + place('!=') + '\n' + b[m.start() + 1:close_i + 1] + '\n' + place('==') +
                b[end:])
    out = map_main(text, body)
    if changed:
        out = insert_globals(out, '#ifndef SPIRV_CROSS_CONSTANT_ID_907\n#define SPIRV_CROSS_CONSTANT_ID_907 1u\n#endif\n'
                                  'const uint g_banjoEarlyHoist = SPIRV_CROSS_CONSTANT_ID_907;\n')
    return out


# ---------------------------------------------------------------------------------------------------------------------
# pass: ifmerge — consecutive ifs on the same unchanged condition variable as one

IF_VAR_RE = re.compile(r'\n([ \t]*)if \((!?)(_\d+)\)\n\1\{')
DECL_ONLY_RE = re.compile(r'\n([ \t]*)(?:float|int|uint|bool|[biu]?vec[234]|mat[234]) _\d+;')


def _if_end(b, m):
    """For an if matched by IF_VAR_RE: (then body, else body or None, end index)."""
    open_i = b.index('{', m.end() - 1)
    close_i = matching(b, open_i, '{', '}')
    if close_i < 0:
        return None
    then_body = b[open_i + 1:close_i]
    indent = m.group(1)
    em = re.match(r'\n' + re.escape(indent) + r'else\n' + re.escape(indent) + r'\{', b[close_i + 1:])
    if not em:
        return then_body, None, close_i + 1
    eo = close_i + 1 + em.end() - 1
    ec = matching(b, eo, '{', '}')
    if ec < 0:
        return None
    return then_body, b[eo + 1:ec], ec + 1


def pass_ifmerge(text):
    """SPIRV-Cross writes Renut's per-pixel branches as one if per value: the fur shader's shadow cascades test `_678`
    in four ifs in a row and `_914` in three (each its own branches and, the condition being per pixel, its own
    reconvergence: SSY, BRA, SYNC). Two ifs at the same level on the same condition variable (or its negation, the
    same both times), with only declarations between them and nothing in the first assigning the variable, are one if:
    the first's then and the second's then, the first's else and the second's else, in that order (the same statements
    run in the same order on each path). The declarations go ahead of the first."""
    def body(b):
        pos = 0
        while True:
            m = IF_VAR_RE.search(b, pos)
            if not m:
                return b
            first = _if_end(b, m)
            if not first:
                pos = m.end()
                continue
            then1, else1, end1 = first
            indent, neg, var = m.group(1), m.group(2), m.group(3)
            # declarations only between
            at = end1
            decls = []
            while True:
                dm = DECL_ONLY_RE.match(b, at)
                if not dm or dm.group(1) != indent:
                    break
                decls.append(b[at + 1:dm.end()].strip())
                at = dm.end()
            m2 = IF_VAR_RE.match(b, at)
            assigned = re.compile(r'\b' + re.escape(var) + r'\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)')
            if (not m2 or m2.group(1) != indent or m2.group(2) != neg or m2.group(3) != var or
                    assigned.search(then1) or (else1 is not None and assigned.search(else1))):
                pos = m.end()
                continue
            second = _if_end(b, m2)
            if not second:
                pos = m.end()
                continue
            then2, else2, end2 = second
            merged = (''.join(f'\n{indent}{d}' for d in decls) +
                      f'\n{indent}if ({neg}{var})\n{indent}{{{then1.rstrip()}{then2.rstrip()}\n{indent}}}')
            if else1 is not None or else2 is not None:
                merged += f'\n{indent}else\n{indent}{{{(else1 or "").rstrip()}{(else2 or "").rstrip()}\n{indent}}}'
            b = b[:m.start()] + merged + b[end2:]
            pos = m.start()  # a third may follow
    return map_main(text, body)


# ---------------------------------------------------------------------------------------------------------------------
# pass: dowhile — `do { ... break; } while(false)` early exits as if / else

DO_RE = re.compile(r'\n([ \t]*)do\n\1\{')
LOOP_START_RE = re.compile(r'(do|for|while|switch)\b')


def _statements(body):
    """The top-level statements of a block's text: (start, end) spans; None when it has one this does not parse."""
    out = []
    i, n = 0, len(body)
    while True:
        while i < n and body[i] in ' \t\n':
            i += 1
        if i >= n:
            return out
        if body[i] == '{':
            j = matching(body, i, '{', '}')
            if j < 0:
                return None
            out.append((i, j + 1))
            i = j + 1
            continue
        m = re.match(r'(if|for|while|switch)\s*\(', body[i:])
        if m:
            p0 = i + m.end() - 1
            p1 = matching(body, p0, '(', ')')
            if p1 < 0:
                return None
            k = p1 + 1
            while k < n and body[k] in ' \t\n':
                k += 1
            if k >= n or body[k] != '{':
                return None
            e = matching(body, k, '{', '}')
            if e < 0:
                return None
            end = e + 1
            if m.group(1) == 'if':
                em = re.match(r'\s*else\b', body[end:])
                while em:
                    k = end + em.end()
                    while k < n and body[k] in ' \t\n':
                        k += 1
                    if body.startswith('if', k):  # else if: one more if statement
                        q0 = body.index('(', k)
                        q1 = matching(body, q0, '(', ')')
                        k = q1 + 1
                        while k < n and body[k] in ' \t\n':
                            k += 1
                    if k >= n or body[k] != '{':
                        return None
                    e = matching(body, k, '{', '}')
                    if e < 0:
                        return None
                    end = e + 1
                    em = re.match(r'\s*else\b', body[end:])
            out.append((i, end))
            i = end
            continue
        if body.startswith('do', i) and re.match(r'do\s*\{', body[i:]):
            k = body.index('{', i)
            e = matching(body, k, '{', '}')
            if e < 0:
                return None
            m2 = re.match(r'\s*while\s*\([^;]*\)\s*;', body[e + 1:])
            if not m2:
                return None
            out.append((i, e + 1 + m2.end()))
            i = e + 1 + m2.end()
            continue
        depth = 0
        j = i
        while j < n:
            c = body[j]
            if c in '([':
                depth += 1
            elif c in ')]':
                depth -= 1
            elif c == '{':
                return None
            elif c == ';' and depth == 0:
                break
            j += 1
        if j >= n:
            return None
        out.append((i, j + 1))
        i = j + 1


def _breaks_outside_loops(text):
    """break / continue statements of 'text' that are not inside a nested loop or switch (they would leave ours)."""
    found = 0
    i = 0
    while True:
        m = re.search(r'\b(break|continue|do|for|while|switch)\b', text[i:])
        if not m:
            return found
        k = i + m.start()
        w = m.group(1)
        if w in ('break', 'continue'):
            found += 1
            i = k + len(w)
            continue
        # a nested loop or switch: skip its body (and a do's `while (...);`)
        b = text.find('{', k)
        if b < 0:
            return found + 1
        e = matching(text, b, '{', '}')
        if e < 0:
            return found + 1
        i = e + 1
        if w == 'do':
            wm = re.match(r'\s*while\s*\([^;]*\)\s*;', text[i:])
            if wm:
                i += wm.end()


def pass_dowhile(text):
    """SPIRV-Cross writes the early exits of Renut's shaders (the texture-array choice, the early alpha test, the main
    body's three fog / colour paths) as `do { ...; if (c) { ...; break; } ...; break; } while(false);`: uam compiles it
    as a loop (PBK, BRK, a loop for the value numbering of 2.26). When its breaks are only the last statement of ifs at
    its top level (no else) and its last statement, it is if / else: `if (c) { A } else { rest }`, in a block of its
    own. The same statements run on each path in the same order."""
    def convert(body):
        stmts = _statements(body)
        if stmts is None:
            return None
        pieces = []
        for idx, (a, b) in enumerate(stmts):
            st = body[a:b]
            ls = body.rfind('\n', 0, a) + 1
            ind = body[ls:a] if not body[ls:a].strip() else ''
            last = idx == len(stmts) - 1
            if st.strip() == 'break;':
                if not last:
                    return None
                pieces.append(('end', None))
                continue
            if st.startswith('if'):
                p0 = st.index('(')
                p1 = matching(st, p0, '(', ')')
                k = st.index('{', p1)
                e = matching(st, k, '{', '}')
                inner = st[k + 1:e]
                rest = st[e + 1:].strip()
                istmts = _statements(inner)
                if istmts and inner[istmts[-1][0]:istmts[-1][1]].strip() == 'break;' and not rest:
                    head = inner[:istmts[-1][0]]
                    if _breaks_outside_loops(head):
                        return None
                    pieces.append(('exit', (ind, st[:p1 + 1], head)))
                    continue
            if _breaks_outside_loops(st):
                return None
            pieces.append(('stmt', ind + st))
        if any(k == 'end' for k, _ in pieces[:-1]):
            return None
        # nest from the last exit outwards
        out = ''
        for kind, val in reversed(pieces):
            if kind == 'end':
                continue
            if kind == 'stmt':
                out = val + '\n' + out
            else:
                ind, cond, head = val
                out = f'{ind}{cond}\n{ind}{{{head.rstrip()}\n{ind}}}\n{ind}else\n{ind}{{\n{out}{ind}}}\n'
        return out

    def body(b):
        pos = 0
        while True:
            m = DO_RE.search(b, pos)
            if not m:
                return b
            open_i = b.index('{', m.end() - 1)
            close_i = matching(b, open_i, '{', '}')
            if close_i < 0:
                return b
            wm = re.match(r'\s*while\s*\(\s*false\s*\)\s*;', b[close_i + 1:])
            if not wm:
                pos = m.end()
                continue
            inner = b[open_i + 1:close_i]
            if 'continue' in inner and _breaks_outside_loops(inner) and re.search(r'\bcontinue\b', inner):
                pos = m.end()
                continue
            new = convert(inner)
            if new is None:
                pos = m.end()
                continue
            ind = m.group(1)
            b = b[:m.start()] + f'\n{ind}{{\n' + new + f'{ind}}}' + b[close_i + 1 + wm.end():]  # its own block
            pos = m.start()
    return map_main(text, body)


PASSES = [
    ('scale1', pass_scale1),
    ('fragscale', pass_fragscale),
    ('alpha', pass_alpha),
    ('tess', pass_tess),
    ('bonerow', pass_bonerow),
    ('vszero', pass_vszero),
    ('vsout', pass_vsout),
    ('pszero', pass_pszero),
    ('pred', pass_pred),
    ('predmul', pass_predmul),
    ('sext', pass_sext),
    ('bool01', pass_bool01),
    ('sel', pass_sel),
    ('alphamask', pass_alphamask),
    ('earlyhoist', pass_earlyhoist),
    ('ifmerge', pass_ifmerge),
    ('dowhile', pass_dowhile),
]
DEFAULT = ','.join(name for name, _ in PASSES)


# The passes for one stage only.
STAGE_OF = {'alpha': 'frag', 'alphamask': 'frag', 'earlyhoist': 'frag', 'tess': 'vert', 'vsout': 'vert', 'vszero': 'vert', 'pszero': 'frag'}


def optimise(text, passes=DEFAULT, stage=None):
    """stage: 'vert' or 'frag' (from the text when not given: a vertex shader writes gl_Position)."""
    names = [p for p in passes.split(',') if p] if isinstance(passes, str) else list(passes)
    if stage is None:
        stage = 'vert' if re.search(r'\bgl_Position\b', text) else 'frag'
    for name, fn in PASSES:
        if name in names and STAGE_OF.get(name, stage) == stage:
            text = fn(text)
    return text


UAM = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out', 'deko3d_tools', 'uam-build',
                   'uam.exe')


def check_one(item):
    """uam compiles the shader's default variant (and with the alpha test's function 7 and 8): '' or the error."""
    import subprocess
    import tempfile
    name, text = item
    stage = 'frag' if name.startswith('ps_') else 'vert'
    variants = [text]
    if 'SPIRV_CROSS_CONSTANT_ID_903' in text:
        nl = text.find('\n') + 1
        variants += [text[:nl] + f'#define SPIRV_CROSS_CONSTANT_ID_903 {f}u\n' + text[nl:] for f in (7, 8)]
    with tempfile.TemporaryDirectory() as tmp:
        for k, v in enumerate(variants):
            src = os.path.join(tmp, f'v{k}.glsl')
            out = os.path.join(tmp, f'v{k}.dksh')
            with open(src, 'w', encoding='utf-8', newline='\n') as f:
                f.write(v)
            r = subprocess.run([UAM, '-s', stage, '-o', out, src], capture_output=True, text=True)
            if r.returncode != 0 or not os.path.isfile(out):
                return name, (r.stdout + r.stderr).strip()[:300]
    return name, ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in', dest='src')
    ap.add_argument('--out')
    ap.add_argument('--passes', default=DEFAULT)
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--check', action='store_true',
                    help='compile each output with uam; a shader that fails keeps its text as it came in')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    if a.list:
        for name, fn in PASSES:
            print(f'{name}: {fn.__doc__.split(".")[0].strip()}')
        return 0
    os.makedirs(a.out, exist_ok=True)
    sources = {}
    texts = {}
    for name in sorted(os.listdir(a.src)):
        if not re.fullmatch(r'(vs|ps)_[0-9A-F]{16}\.glsl', name):
            continue
        sources[name] = open(os.path.join(a.src, name), encoding='utf-8').read()
        texts[name] = optimise(sources[name], a.passes, 'frag' if name.startswith('ps_') else 'vert')
    failed = []
    if a.check:
        import concurrent.futures
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
            for name, err in ex.map(check_one, texts.items()):
                if err:
                    failed.append(name)
                    if len(failed) <= 10:
                        print(f'  {name}: uam failed, kept as it was: {err}')
                    texts[name] = sources[name]
    for name, out in texts.items():
        with open(os.path.join(a.out, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(out)
    print(f'deko3d_glsl_opt: {len(texts)} shaders, passes {a.passes} -> {a.out}' +
          (f' ({len(failed)} kept as they were: uam failed)' if a.check else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
