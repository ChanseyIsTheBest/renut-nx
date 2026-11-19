#!/usr/bin/env python3
"""banjo-nx (2026-11-20): a thread's sampled time per function, per draw, from a stack-sampled rex_profile.log and the
renut log of the same run: two builds on the same scene compared function by function in absolute terms (shares alone
hide a uniform slowdown).

Each window's samples of the thread (user PCs and kernel waits) are its 10 s of wall time; a function's time is its
PCs' share of those samples times 10 s; summed over the windows from --from on, and divided by the draws the renut log
counts in its 10 s lines over the same span (the "[renut] 10.0 s: ... N draws" lines from --from on).

  python tools/profile_per_draw.py PROFILE ELF RENUT_LOG [PROFILE ELF RENUT_LOG] [--from 70] [--thread 0x2C/0x2] [--top 30]
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import profile_threads as pt  # noqa: E402

DRAWS = re.compile(r'\[renut\] 1\d\.\d s: (\d+) game frames .*; (\d+) draws')


def run(profile, elf, log, t0, key):
    table = pt.symbols(elf)
    funcs = collections.Counter()
    windows = 0
    win = -1
    cur = None
    samples = 0
    for line in open(profile, encoding='utf-8', errors='replace'):
        if pt.WIN.match(line):
            win += 1
            cur = None
            continue
        if win * 10 < t0:
            continue
        m = pt.THREAD.match(line)
        if m:
            k = '%s/%s' % (m.group(4).upper().replace('0X', '0x'), m.group(6).lower())
            cur = k == key
            continue
        if not cur:
            continue
        m = pt.SAMPLES.match(line)
        if m:
            samples = int(m.group(1))
            windows += 1
            continue
        m = pt.PC.match(line)
        if m and samples:
            # this window's share of the thread's 10 s, in microseconds
            name = re.sub(r' \[clone [^]]*\]', '', pt.name_of(table, int(m.group(1), 16)))  # LTO's clones
            funcs[name] += int(m.group(2)) / samples * 10e6
    draws = 0
    frames = 0
    n = 0
    for line in open(log, encoding='utf-8', errors='replace'):
        m = DRAWS.search(line)
        if m:
            n += 1
            if n * 10 >= t0 and n <= windows + t0 // 10:
                frames += int(m.group(1))
                draws += int(m.group(2))
    return funcs, windows, draws, frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--from', dest='t0', type=float, default=70)
    ap.add_argument('--thread', default='0x2C/0x2')
    ap.add_argument('--top', type=int, default=30)
    a = ap.parse_args()
    runs = []
    for i in range(0, len(a.files), 3):
        prof, elf, log = a.files[i:i + 3]
        funcs, windows, draws, frames = run(prof, elf, log, a.t0, a.thread)
        runs.append((prof, funcs, windows, draws))
        total = sum(funcs.values())
        print('== %s: %d windows of the thread, %d draws (%d frames); user code %.2f us a draw' % (
            prof, windows, draws, frames, total / max(1, draws)))
    names = collections.Counter()
    for _, funcs, _, draws in runs:
        for f, us in funcs.items():
            names[f] = max(names[f], us / max(1, draws))
    print('%-70s ' % 'us a draw' + ' '.join('%10s' % ('run %d' % (i + 1)) for i in range(len(runs))))
    for f, _ in names.most_common(a.top):
        print('%-70s ' % f[:70] + ' '.join('%10.3f' % (r[1].get(f, 0) / max(1, r[3])) for r in runs))


if __name__ == '__main__':
    main()
