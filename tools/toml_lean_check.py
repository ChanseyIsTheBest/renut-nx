"""Checks the lean renut.toml (2026-11-01): for every key of the old long renut.toml, the Switch value it gets now (the
lean toml's line, else the BANJO_SWITCH_DEFAULT Switch value, else the code default) against the old value.
    python tools/toml_lean_check.py <old renut.toml> <lean renut.toml>   (run from banjo-nx)"""
import os
import re
import sys
import tomllib

old = tomllib.load(open(sys.argv[1], 'rb'))
lean = tomllib.load(open(sys.argv[2], 'rb'))
pat = re.compile(r'REXCVAR_DEFINE_\w+\(\s*([A-Za-z_0-9]+)\s*,\s*(BANJO_SWITCH_DEFAULT\((?:"[^"]*"|[^,()])+,(?:"[^"]*"|[^()])+\)|'
                 r'(?:"[^"]*"|[^,])+?)\s*,', re.S)
defs = {}
for root in ['app/src', 'sdk/src', 'sdk/include', 'app/generated']:
    for dp, dn, fn in os.walk(root):
        if 'thirdparty' in dp:
            continue
        for f in fn:
            if f.endswith(('.cpp', '.h', '.inc', '.cc')):
                s = open(os.path.join(dp, f), encoding='utf-8', errors='replace').read()
                for m in pat.finditer(s):
                    defs.setdefault(m.group(1), m.group(2).strip())
hot = open('app/generated/hot/banjo_hot_list.inc', encoding='utf-8').read()
for m in re.finditer(r'BANJO_HOT_HOOK\([^,]+,\s*[^,]+,\s*([A-Za-z_0-9]+),\s*(true|false)', hot):
    defs.setdefault(m.group(1), m.group(2))


def value(text):
    text = text.strip()
    m = re.match(r'BANJO_SWITCH_DEFAULT\(((?:"[^"]*"|[^,])+),', text)
    if m:
        text = m.group(1).strip()
    text = text.strip('()').strip()
    if text.startswith('"'):
        return text.strip('"')
    if text in ('true', 'false'):
        return text == 'true'
    try:
        return int(text, 0)
    except ValueError:
        return float(text.rstrip('f'))


bad = 0
for k, v in old.items():
    if isinstance(v, dict):
        if lean.get(k) != v:
            print('SECTION', k, 'differs')
            bad += 1
        continue
    if k in lean:
        now, src = lean[k], 'lean toml'
    elif k in defs:
        try:
            now, src = value(defs[k]), 'default'
        except ValueError:
            now, src = defs[k], 'default (unparsed)'
    else:
        now, src = None, 'NO DEFINITION'
    same = now == v or (isinstance(v, float) and isinstance(now, (int, float)) and float(now) == v)
    if not same:
        bad += 1
        print(f'{k}: old {v!r} now {now!r} ({src})')
print(f'{bad} differ of {len(old)}')
