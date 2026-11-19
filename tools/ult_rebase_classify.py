#!/usr/bin/env python3
"""Classify every file for banjo-nx's rebase on the ultimate base (2026-11-19).

Revisions (all in banjo-nx's object store; the base fetched into refs/ultbase/ultimate):
  S  the base's first commit (83b4ae77): banjo-nx's structure as copied on 2026-10-30
  K  katamari-nx 1.0.1 (701c9d32): the end of the Katamari era, the last commit before the ultimate ports
  T  the base's tip (ports 1-188 or later)
  B  banjo-nx's master (renut-nx 1.0.2 and its docs commits)

Contents are compared with CR removed (the two repositories store some files with different line ends).
Writes a TSV: path, class, and which of S/K/T/B hold the file, to out/ult_rebase_classes.tsv.

  same       T == B
  base       B == S (Banjo did not change it since the fork): take T
  banjo      T == S (the base did not change it since the fork): take B
  merge      both changed since S
  base-new   only in T (and not in S)
  banjo-new  only in B (and not in S)
  base-del   in S and B (B == S) but not in T: the base deleted it
  banjo-del  in S and T (T == S) but not in B: Banjo deleted it
  del-both / conflict-del  the rest
"""
import os
import subprocess
import sys

REVS = {'S': '83b4ae77', 'K': '701c9d32', 'T': 'refs/ultbase/ultimate', 'B': 'master'}


def tree(rev):
    out = subprocess.run(['git', 'ls-tree', '-r', rev], capture_output=True, check=True).stdout
    files = {}
    for line in out.split(b'\n'):
        if not line:
            continue
        meta, path = line.split(b'\t', 1)
        mode, kind, sha = meta.split()
        if kind != b'blob':
            continue
        files[path.decode('utf-8', 'surrogateescape')] = sha.decode()
    return files


class Blobs:
    def __init__(self):
        self.p = subprocess.Popen(['git', 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.cache = {}

    def get(self, sha):
        if sha in self.cache:
            return self.cache[sha]
        self.p.stdin.write(sha.encode() + b'\n')
        self.p.stdin.flush()
        header = self.p.stdout.readline().split()
        size = int(header[2])
        data = self.p.stdout.read(size)
        self.p.stdout.read(1)
        norm = data.replace(b'\r\n', b'\n')
        self.cache[sha] = norm
        return norm


def main():
    trees = {k: tree(v) for k, v in REVS.items()}
    blobs = Blobs()
    paths = sorted(set().union(*[set(t) for t in trees.values()]))

    def content(k, p):
        sha = trees[k].get(p)
        return None if sha is None else blobs.get(sha)

    rows = []
    for p in paths:
        s, k, t, b = (content(x, p) for x in 'SKTB')
        have = ''.join(x for x, c in zip('SKTB', (s, k, t, b)) if c is not None)
        if t is not None and b is not None and t == b:
            cls = 'same'
        elif t is not None and b is not None:
            if b == s:
                cls = 'base'
            elif t == s:
                cls = 'banjo'
            else:
                cls = 'merge'
        elif t is not None:  # not in B
            if s is None:
                cls = 'base-new'
            elif t == s:
                cls = 'banjo-del'
            else:
                cls = 'conflict-del'
        elif b is not None:  # not in T
            if s is None:
                cls = 'banjo-new'
            elif b == s:
                cls = 'base-del'
            else:
                cls = 'conflict-del'
        else:
            cls = 'del-both'
        kat = ''
        if s is not None or k is not None:
            kat = 'K' if s != k else ''
        rows.append((p, cls, have, kat))
    os.makedirs('out', exist_ok=True)
    with open('out/ult_rebase_classes.tsv', 'w', encoding='utf-8') as f:
        f.write('path\tclass\tin\tchanged_in_katamari_era\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    counts = {}
    for _, cls, _, _ in rows:
        counts[cls] = counts.get(cls, 0) + 1
    for cls, n in sorted(counts.items()):
        print('%-13s %d' % (cls, n))


if __name__ == '__main__':
    sys.exit(main())
