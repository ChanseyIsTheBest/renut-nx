"""renut-nx (2026-11-22): NAME.opcm for Nuts & Bolts' music, on a PC (the web installer's lib/opcm.js does the same in the
browser). rr6-nx's tools/opus_pcm_pack.py (credit rr6-nx; its file format, rex/filesystem/opus_pcm.h) with one more kind
of file it finds PCM in: XACT 3 wave banks (version 42-46, the Xbox 360's: big-endian, signature 'DNBW') anywhere in
the file. N&B's are inside the bundle Bundle/50/685374 (the first at byte 97,148): 15 stereo 44.1 kHz music tracks
(507 MB) and 9 short mono sounds, 16-bit big-endian PCM. Each sound's 24-byte entry: flags and duration, the format word
(read big-endian, in the PC's bit order: tag 2 bits (0 PCM), channels 3, rate 18, block align 8, bits 1 (1: 16-bit)),
the play region (offset in the bank's wave data, length). The XMA sounds stay as they are.

  python -I tools/xwb_opcm_pack.py --rr6 RR6_TOOL IN OUT.opcm [--bitrate 128000] [--ffmpeg C:/ffmpeg/ffmpeg.exe]
  (--check / --compare as rr6-nx's tool)
"""
import importlib.util
import struct
import sys


def load_rr6(path):
    spec = importlib.util.spec_from_file_location('opus_pcm_pack', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def regions_xwb(mod, data):
    out = []
    at = data.find(b'DNBW')
    while at >= 0 and at + 64 <= len(data):
        base = at
        version = struct.unpack_from('>I', data, base + 4)[0]
        ok = 42 <= version <= 46
        if ok:
            seg = [struct.unpack_from('>II', data, base + 12 + 8 * k) for k in range(5)]
            ok = all(base + o + n <= len(data) for o, n in (seg[0], seg[1], seg[4]))
        if ok:
            bank, (meta, meta_len), (wave, wave_len) = seg[0][0], seg[1], seg[4]
            count = struct.unpack_from('>I', data, base + bank + 4)[0]
            meta_size = struct.unpack_from('>I', data, base + bank + 72)[0]
            ok = meta_size == 24 and count * 24 <= meta_len
        if ok:
            name = data[base + bank + 8:base + bank + 72].split(b'\0')[0].decode('ascii', 'replace')
            for i in range(count):
                e = base + meta + 24 * i
                w, off, length = struct.unpack_from('>III', data, e + 4)
                tag, ch, rate, bits16 = w & 3, (w >> 2) & 7, (w >> 5) & 0x3FFFF, w >> 31
                if tag != 0 or not bits16 or ch not in (1, 2) or length < 65536 or off + length > wave_len:
                    continue
                start = base + wave + off
                usable = length - length % (ch * 2)
                out.append(mod.Region(f'{name} {i}', ch, rate, mod.guess_big(data[start:start + min(length, 1 << 22)]),
                                      [(start, usable)]))
            at = data.find(b'DNBW', base + wave + wave_len)
        else:
            at = data.find(b'DNBW', at + 1)
    return out


def main():
    if '--rr6' not in sys.argv:
        raise SystemExit('--rr6 PATH: rr6-nx\'s tools/opus_pcm_pack.py')
    k = sys.argv.index('--rr6')
    mod = load_rr6(sys.argv[k + 1])
    del sys.argv[k:k + 2]
    inner = mod.find_regions

    def find_regions(data):
        regions, cut = inner(data)
        if regions or data[:4] in (b'LIVE', b'PIRS', b'CON '):
            return regions, cut
        return regions_xwb(mod, data), None

    mod.find_regions = find_regions
    mod.main()


if __name__ == '__main__':
    main()
