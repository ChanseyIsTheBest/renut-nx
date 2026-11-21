"""banjo-nx (2026-11-21): per-pair A/B deltas from the [renut A/B] lines (steady city halves only: > 4000 draws, < 20 us a draw on both):
render thread, GPU and all-threads CPU, each a draw, mean / se / how many pairs had 'on' higher.
Usage: ab_pairs.py LOG [TARGET ...]"""
import math
import re
import sys

R = re.compile(r'\(target (\d+), banjo_renut_ab_targets\): on ([0-9.]+) fps, render thread ([0-9.]+) ms a frame, GPU '
               r'([0-9.]+) ms a frame \(8 s\), (?:CPU ([0-9.]+) ms a frame \(all threads\), )?(\d+) draws a frame, '
               r'render thread ([0-9.]+) us a draw; off ([0-9.]+) fps, render thread ([0-9.]+) ms a frame, GPU ([0-9.]+) '
               r'ms a frame \(8 s\), (?:CPU ([0-9.]+) ms a frame \(all threads\), )?(\d+) draws a frame, render thread '
               r'([0-9.]+) us a draw')
log = sys.argv[1]
want = set(sys.argv[2:])
by = {}
for line in open(log, encoding='utf-8', errors='replace'):
    m = R.search(line)
    if not m:
        continue
    t = m.group(1)
    if want and t not in want:
        continue
    g = m.groups()
    on = dict(rt=float(g[6]), gpu=float(g[3]), cpu=float(g[4] or 0), d=int(g[5]), fps=float(g[1]))
    off = dict(rt=float(g[12]), gpu=float(g[9]), cpu=float(g[10] or 0), d=int(g[11]), fps=float(g[7]))
    if min(on['d'], off['d']) <= 4000 or max(on['rt'], off['rt']) >= 20:
        continue
    by.setdefault(t, []).append((on, off))


def stat(xs):
    n = len(xs)
    m = sum(xs) / n
    sd = math.sqrt(max(sum(x * x for x in xs) / n - m * m, 0))
    return m, sd / math.sqrt(n), sum(1 for x in xs if x > 0), n


for t, ps in sorted(by.items()):
    out = []
    for key, name in (('rt', 'render'), ('gpu', 'GPU'), ('cpu', 'CPU all')):
        xs = []
        for on, off in ps:
            if on[key] and off[key]:
                a = on[key] / on['d'] if key != 'rt' else on[key]
                b = off[key] / off['d'] if key != 'rt' else off[key]
                xs.append(100 * (a / b - 1))
        if xs:
            m, se, up, n = stat(xs)
            out.append('%s %+.2f %% (se %.2f, on higher %d/%d)' % (name, m, se, up, n))
    fps = stat([100 * (on['fps'] / off['fps'] - 1) for on, off in ps])
    print('A/B %s, a draw: %s; fps %+.2f %%' % (t, '; '.join(out), fps[0]))
