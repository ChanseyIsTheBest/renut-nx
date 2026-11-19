#!/usr/bin/env python3
"""Resolve one diff3 conflict block of a file by side (2026-11-19, the rebase on the ultimate base).

  python tools/ult_resolve.py FILE              list the blocks (number, line, sizes)
  python tools/ult_resolve.py FILE N SIDE       replace block N (1-based) by SIDE: ours, base, theirs, both (ours then
                                                theirs), or @PATH (the text of a file)
  python tools/ult_resolve.py FILE N show       print block N
"""
import sys


def blocks(lines):
    out, i = [], 0
    while i < len(lines):
        if lines[i].startswith('<<<<<<< '):
            start = i
            j = i + 1
            while not lines[j].startswith('||||||| '):
                j += 1
            k = j + 1
            while lines[k] != '=======\n' and lines[k] != '=======':
                k += 1
            m = k + 1
            while not lines[m].startswith('>>>>>>> '):
                m += 1
            out.append((start, j, k, m))
            i = m + 1
        else:
            i += 1
    return out


def main():
    path = sys.argv[1]
    lines = open(path, encoding='utf-8', newline='').read().splitlines(keepends=True)
    bl = blocks(lines)
    if len(sys.argv) == 2:
        for n, (s, j, k, m) in enumerate(bl, 1):
            print('%d: line %d, ours %d, base %d, theirs %d lines' % (n, s + 1, j - s - 1, k - j - 1, m - k - 1))
        return 0
    n = int(sys.argv[2])
    s, j, k, m = bl[n - 1]
    ours, base, theirs = lines[s + 1:j], lines[j + 1:k], lines[k + 1:m]
    side = sys.argv[3]
    if side == 'show':
        sys.stdout.write(''.join(lines[s:m + 1]))
        return 0
    if side == 'ours':
        new = ours
    elif side == 'base':
        new = base
    elif side == 'theirs':
        new = theirs
    elif side == 'both':
        new = ours + theirs
    elif side.startswith('@'):
        new = open(side[1:], encoding='utf-8').read().splitlines(keepends=True)
    else:
        raise SystemExit('side?')
    lines[s:m + 1] = new
    # Through a temporary file, so a failed write (a scanner holding the file) never leaves it truncated.
    import os
    import time
    tmp = path + '.ult_tmp'
    open(tmp, 'w', encoding='utf-8', newline='').write(''.join(lines))
    for attempt in range(20):
        try:
            os.replace(tmp, path)
            break
        except OSError:
            time.sleep(0.5)
    else:
        raise SystemExit('could not replace ' + path + ' (left in ' + tmp + ')')
    print('%s: block %d -> %s (%d lines); %d left' % (path, n, side, len(new), len(bl) - 1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
