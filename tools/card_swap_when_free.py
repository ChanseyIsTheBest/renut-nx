#!/usr/bin/env python3
"""banjo-nx (2026-11-20): once N&B is no longer running on the console (no log under logs/ held open, and the running
title, when sys-botbase answers, not the forwarder), put the card back to this branch's build: renames only (the tool
has no delete), 1.0.2's files under their -1.0.2 names. Polls every 2 minutes; gives up after --hours.

  python tools/card_swap_when_free.py [--hours 7] [--toml dist/switch/renut-nx/renut.toml]
"""
import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, 'tools', 'switch_remote.py')
FORWARDER = '059270E176E83000'
SWAPS = [  # (from, to), in order; the tool refuses a rename onto an existing name
    ('renut-nx.nro', 'renut-nx-1.0.2.nro'),
    ('renut-nx-ult.nro', 'renut-nx.nro'),
    ('deko3d_shaders.bin', 'deko3d_shaders-1.0.2.bin'),
    ('deko3d_shaders-ult.bin', 'deko3d_shaders.bin'),
    ('deko3d_glsl.bin', 'deko3d_glsl-1.0.2.bin'),
    ('pipeline_list.bin', 'pipeline_list-1.0.2.bin'),
    ('pipeline_list-ult.bin', 'pipeline_list.bin'),
]


def remote(*args, timeout=120):
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    r = subprocess.run([sys.executable, TOOL, '--root', '/switch/renut-nx', *args], capture_output=True, text=True,
                       timeout=timeout, env=env)
    return r.returncode, (r.stdout + r.stderr).strip()


def log(msg):
    line = time.strftime('%H:%M:%S ') + msg
    print(line, flush=True)
    with open(os.path.join(ROOT, 'out', 'card', 'swap.log'), 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def names():
    rc, out = remote('ls')
    return {line.split()[-1]: line for line in out.splitlines() if line.strip()} if rc == 0 else None


def free():
    rc, out = remote('ls', '/switch/renut-nx/logs')
    if rc != 0:
        return False
    if any(line.startswith('----------') for line in out.splitlines()):
        return False  # a log held open: the game runs
    try:
        rc, title = remote('title', timeout=30)
    except subprocess.TimeoutExpired:
        rc, title = 1, ''
    if rc == 0 and FORWARDER in title.upper():
        return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hours', type=float, default=7)
    ap.add_argument('--toml', default=os.path.join(ROOT, 'dist', 'switch', 'renut-nx', 'renut.toml'))
    a = ap.parse_args()
    end = time.time() + a.hours * 3600
    log('waiting for the console to be free of N&B')
    while time.time() < end:
        try:
            if free():
                break
        except Exception as e:  # a network hiccup: try again
            log('poll failed: %s' % e)
        time.sleep(120)
    else:
        log('gave up: N&B still running after %.1f h' % a.hours)
        return 1
    have = names()
    if have is None:
        log('cannot list the folder')
        return 1
    for src, dst in SWAPS:
        if src not in have:
            log('skip %s -> %s (no %s)' % (src, dst, src))
            continue
        if dst in have:
            log('skip %s -> %s (%s exists)' % (src, dst, dst))
            continue
        rc, out = remote('mv', src, dst)
        log(out.splitlines()[-1] if out else 'mv %s %s: %d' % (src, dst, rc))
        have = names() or have
    rc, out = remote('put', a.toml, 'renut.toml')
    log(out.splitlines()[-1] if out else 'put toml: %d' % rc)
    rc, out = remote('ls')
    log('folder now:\n' + out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
