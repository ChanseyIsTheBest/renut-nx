#!/usr/bin/env python3
"""renut-nx (2026-11-22): culdcept-nx's tools/hevc_movie.py as it is (credit culdcept-nx), for N&B's movies (Debug/**,
the port's hook: app/src/cpu/banjo_wmv_ffmpeg.cpp). Its own notes:

culdcept-nx (2026-11-20, the user: hardware H.265 movies): an H.265 copy of one of the game's WMV movies, as the port's
movie hook plays it (app/src/cpu/culdcept_wmv_ffmpeg.cpp: a movie whose first frame is H.265 goes to nxvideo, NVDEC on
the Switch). The prototype of what the installer does in the browser (its ASF remux follows this file).

  python tools/hevc_movie.py IN.wmv OUT.wmv [--ffmpeg C:/ffmpeg/ffmpeg.exe] [--encoder hevc_nvenc|libx265] [--q 28]
                             [--gop 120]

1. FFmpeg decodes the WMV3 video and encodes it as raw H.265 (Annex B): no B frames (one picture out per frame in), an
   access unit delimiter on every frame, a keyframe with its parameter sets every --gop frames.
2. The original ASF file is read as media objects (its frames: audio and video, in the order they were sent) and written
   again with the same header (the WMV3 stream's properties and sequence header, which the game's player sets its
   decoder up from), the same audio, the same presentation times and the same packet size: each video frame's payload
   is the matching H.265 access unit (frame for frame: the WMV3 movies have one picture per frame and no B frames), the
   key-frame flags follow the H.265 keyframes, the packets are filled again, and the simple index, the data object and
   the file properties are made for the new packets.
Game data: never commit or share what it writes.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import uuid


def guid(s):
    return uuid.UUID(s).bytes_le


HEADER = guid('75B22630-668E-11CF-A6D9-00AA0062CE6C')
DATA = guid('75B22636-668E-11CF-A6D9-00AA0062CE6C')
SIMPLE_INDEX = guid('33000890-E5B1-11CF-89F4-00A0C90349CB')
FILE_PROPERTIES = guid('8CABDCA1-A947-11CF-8EE4-00C00C205365')
STREAM_PROPERTIES = guid('B7DC0791-A9B7-11CF-8EE6-00C00C205365')
VIDEO_MEDIA = guid('BC19EFC0-5B4D-11CF-A8FD-00805F5C442B')


def read_len(buf, i, kind):
    """A field of the ASF length type `kind` (0 none, 1 byte, 2 word, 3 dword): (value, next offset)."""
    if kind == 0:
        return 0, i
    if kind == 1:
        return buf[i], i + 1
    if kind == 2:
        return struct.unpack_from('<H', buf, i)[0], i + 2
    return struct.unpack_from('<I', buf, i)[0], i + 4


class Asf:
    def __init__(self, buf):
        if buf[:16] != HEADER:
            raise ValueError('not an ASF file')
        self.buf = buf
        self.header_size = struct.unpack_from('<Q', buf, 16)[0]
        self.video_stream = None
        i = 30
        while i < self.header_size:
            g = buf[i:i + 16]
            size = struct.unpack_from('<Q', buf, i + 16)[0]
            if g == FILE_PROPERTIES:
                self.fp = i
                (self.min_packet, self.max_packet) = struct.unpack_from('<II', buf, i + 24 + 16 + 8 + 8 + 8 + 8 + 8 + 8 + 4)
                self.preroll = struct.unpack_from('<Q', buf, i + 24 + 16 + 8 + 8 + 8 + 8 + 8)[0]
            elif g == STREAM_PROPERTIES and buf[i + 24:i + 40] == VIDEO_MEDIA:
                flags = struct.unpack_from('<H', buf, i + 24 + 16 + 16 + 8 + 4 + 4)[0]
                self.video_stream = flags & 0x7F
            i += size
        if self.min_packet != self.max_packet:
            raise ValueError('packets of varying size')
        self.packet_size = self.min_packet
        self.data = self.header_size
        if buf[self.data:self.data + 16] != DATA:
            raise ValueError('no data object after the header')
        self.data_size = struct.unpack_from('<Q', buf, self.data + 16)[0]
        self.packets = struct.unpack_from('<Q', buf, self.data + 16 + 8 + 16)[0]
        self.index = self.data + self.data_size  # the objects after the data object (the simple index)

    def media_objects(self):
        """Every media object, in the order its first payload was sent: dicts of stream, number, time (ms), key, send,
        data, sends. 2026-11-20: `sends` is each of its payloads' (offset, packet send time), and self.packets_end the
        last packet's send time plus its duration, for write_asf's send times and durations (as lib/asf.js)."""
        buf, objs, order = self.buf, {}, []
        self.packets_end = 0
        p = self.data + 50
        for _ in range(self.packets):
            start = p
            i = p
            b = buf[i]
            if b & 0x80:  # error correction data
                i += 1 + (b & 0x0F)
            flags, props = buf[i], buf[i + 1]
            i += 2
            multiple = flags & 1
            packet_len, i = read_len(buf, i, (flags >> 5) & 3)
            _, i = read_len(buf, i, (flags >> 1) & 3)  # sequence
            padding, i = read_len(buf, i, (flags >> 3) & 3)
            send, duration = struct.unpack_from('<IH', buf, i)
            self.packets_end = max(self.packets_end, send + duration)
            i += 6
            if not packet_len:
                packet_len = self.packet_size
            end = start + packet_len - padding
            rep_t, off_t, num_t, sn_t = props & 3, (props >> 2) & 3, (props >> 4) & 3, (props >> 6) & 3
            count, len_t = 1, 0
            if multiple:
                count, len_t = buf[i] & 0x3F, (buf[i] >> 6) & 3
                i += 1
            for k in range(count):
                stream_byte = buf[i]
                i += 1
                number, i = read_len(buf, i, num_t)
                offset, i = read_len(buf, i, off_t)
                rep_len, i = read_len(buf, i, rep_t)
                if rep_len == 1:
                    raise ValueError('compressed payloads are not handled')
                obj_size, pts = struct.unpack_from('<II', buf, i)
                rep = bytes(buf[i:i + rep_len])  # size, time and the stream's payload extensions (kept as they are)
                i += rep_len
                if multiple:
                    plen, i = read_len(buf, i, len_t)
                else:
                    plen = end - i
                key = (stream_byte & 0x7F, number)
                o = objs.get(key)
                if o is None or offset == 0 and o['data'] is not None and len(o['data']) >= o['size']:
                    o = {'stream': stream_byte & 0x7F, 'number': number, 'size': obj_size, 'time': pts,
                         'key': bool(stream_byte & 0x80), 'send': send, 'rep': rep, 'data': bytearray(), 'sends': []}
                    objs[key] = o
                    order.append(o)
                o['sends'].append((offset, send))
                o['data'][offset:offset + plen] = buf[i:i + plen]
                i += plen
            p = start + self.packet_size
        for o in order:
            if len(o['data']) != o['size']:
                raise ValueError(f"stream {o['stream']} object {o['number']}: {len(o['data'])} of {o['size']} bytes")
        return order


def access_units(raw):
    """Split an Annex B H.265 stream at its access unit delimiters (NAL type 35): [(bytes, is_keyframe)]."""
    starts = []
    i = 0
    n = len(raw)
    while True:
        j = raw.find(b'\x00\x00\x01', i)
        if j < 0:
            break
        nal = (raw[j + 3] >> 1) & 0x3F
        s = j - 1 if j > 0 and raw[j - 1] == 0 else j
        starts.append((s, nal))
        i = j + 3
    aus = []
    cur = None
    for k, (s, nal) in enumerate(starts):
        e = starts[k + 1][0] if k + 1 < len(starts) else n
        if nal == 35 or cur is None:
            if cur is not None:
                aus.append(cur)
            cur = [bytearray(), False]
        cur[0] += raw[s:e]
        if 16 <= nal <= 21:  # IRAP: BLA, IDR, CRA
            cur[1] = True
    if cur is not None:
        aus.append(cur)
    return [(bytes(a), k) for a, k in aus]


def write_asf(src, objs, out_path):
    """`objs` (media objects, video data replaced) as the new file: src's header, packets of src's size, a new index.

    2026-11-20 (the console: the H.265 movies played slowly and without sound), as lib/asf.js's write(), byte for byte:
    each packet's send time is the original send time of the bytes it starts with (for a replaced object, of the same
    fraction of the original's bytes), never earlier than the packet before's, and its duration the time to the next
    packet's, as the originals have them; and an audio object (any but the video's) is never split across packets, as
    the originals' muxer never splits one (the game's player lost a split one's audio and paced the movie slower). One
    that does not fit in the packet being filled waits for the next packet, which it starts."""
    P = src.packet_size
    packets = []
    first_packet = {}   # id(object) -> [first packet, packet count]
    state = {'pkt': None, 'last_send': 0}
    deferred = []

    def send_at(o, offset):
        f = o.get('sends')
        if not f:
            return o['send']
        x = offset * o['size'] / len(o['data']) if len(o['data']) else 0  # the same fraction of the original object
        t = f[0][1]
        for off, snd in f:
            if off <= x:
                t = snd
            else:
                break
        return t

    def new_packet(o, offset):
        if state['pkt'] is not None:
            packets.append(state['pkt'])
        state['last_send'] = max(state['last_send'], send_at(o, offset))
        # error correction 3, flags 2, BYTE padding 1, send 4, duration 2, payload flags 1
        state['pkt'] = {'send': state['last_send'], 'payloads': [], 'used': 13}

    def place(o, offset, take, over):
        pkt = state['pkt']
        pkt['payloads'].append((o, offset, o['data'][offset:offset + take]))
        pkt['used'] += over + take
        ent = first_packet.setdefault(id(o), [len(packets), 0])
        ent[1] = len(packets) - ent[0] + 1

    def fits(o, over):
        pkt = state['pkt']
        return pkt is not None and len(pkt['payloads']) < 63 and pkt['used'] + over + len(o['data']) <= P

    def flush():  # the waiting whole objects at the start of a new packet (each fits in an empty one)
        while deferred:
            d, dover = deferred[0]
            if not fits(d, dover):
                new_packet(d, 0)
            if not fits(d, dover):
                raise ValueError(f"stream {d['stream']} object {d['number']} ({len(d['data'])} bytes) fits in no packet")
            place(d, 0, len(d['data']), dover)
            deferred.pop(0)

    for o in objs:
        data = o['data']
        over = 9 + len(o['rep'])  # stream, number, offset, replicated length, replicated data, payload length
        if o['stream'] != src.video_stream and 13 + over + len(data) <= P:
            if not deferred and fits(o, over):
                place(o, 0, len(data), over)
            else:
                deferred.append((o, over))  # after the ones already waiting, in their order
            continue
        offset = 0
        while True:
            pkt = state['pkt']
            if pkt is None or len(pkt['payloads']) >= 63 or pkt['used'] + over + 1 > P:
                new_packet(o, offset)
                flush()  # the waiting audio first in the new packet
                pkt = state['pkt']
                if len(pkt['payloads']) >= 63 or pkt['used'] + over + 1 > P:
                    new_packet(o, offset)
                    pkt = state['pkt']
            take = min(P - pkt['used'] - over, len(data) - offset)
            place(o, offset, take, over)
            offset += take
            if offset >= len(data):
                break
    flush()
    if state['pkt'] is not None:
        packets.append(state['pkt'])
    for k, p in enumerate(packets):
        nxt = packets[k + 1]['send'] if k + 1 < len(packets) else max(getattr(src, 'packets_end', 0), p['send'])
        p['duration'] = min(0xFFFF, max(0, nxt - p['send']))

    body = bytearray()
    for p in packets:
        # The originals' layout: error correction (2 bytes), multiple payloads with a BYTE padding length (flags 0x09;
        # a WORD one, 0x11, only when the padding does not fit a byte), BYTE replicated length / object number / stream
        # number and a DWORD offset (properties 0x5D), WORD payload lengths.
        b = bytearray(b'\x82\x00\x00')
        padding = P - p['used']
        if padding <= 255:
            b += bytes([0x09, 0x5D, padding])
        else:
            padding -= 1  # the WORD field takes one more byte
            b += bytes([0x11, 0x5D]) + struct.pack('<H', padding)
        b += struct.pack('<IH', p['send'], p['duration'])
        b.append(0x80 | len(p['payloads']))
        for o, off, chunk in p['payloads']:
            b.append((0x80 if o['key'] else 0) | o['stream'])
            b += struct.pack('<BIB', o['number'] & 0xFF, off, len(o['rep']))
            b += struct.pack('<I', len(o['data'])) + o['rep'][4:]  # the new size; time and extensions as they were
            b += struct.pack('<H', len(chunk))
            b += chunk
        b += bytes(padding)
        if len(b) != P:
            raise AssertionError((len(b), P))
        body += b

    buf = src.buf
    header = bytearray(buf[:src.header_size])
    data_obj = bytearray(buf[src.data:src.data + 50])
    struct.pack_into('<Q', data_obj, 16, 50 + len(body))
    struct.pack_into('<Q', data_obj, 40, len(packets))

    # the simple index: for each interval, the packet where the last video keyframe at or before it starts
    tail = bytearray()
    i = src.index
    while i + 24 <= len(buf):
        g = buf[i:i + 16]
        size = struct.unpack_from('<Q', buf, i + 16)[0]
        if g == SIMPLE_INDEX:
            interval, _max_count, count = struct.unpack_from('<QII', buf, i + 24 + 16)
            keys = [(o['time'] - src.preroll, first_packet[id(o)][0], first_packet[id(o)][1])
                    for o in objs if o['stream'] == src.video_stream and o['key']]
            entries = bytearray()
            k = 0
            max_count = 1
            for e in range(count):
                t_ms = e * interval // 10000
                while k + 1 < len(keys) and keys[k + 1][0] <= t_ms:
                    k += 1
                pn, pc = (keys[k][1], keys[k][2]) if keys else (0, 1)
                max_count = max(max_count, pc)
                entries += struct.pack('<IH', pn, pc)
            obj = bytearray(buf[i:i + 24 + 16 + 16])
            struct.pack_into('<II', obj, 24 + 16 + 8, max_count, count)
            obj += entries
            struct.pack_into('<Q', obj, 16, len(obj))
            tail += obj
        else:
            tail += buf[i:i + size]
        i += size

    total = len(header) + 50 + len(body) + len(tail)
    fp = src.fp
    struct.pack_into('<Q', header, fp + 24 + 16, total)              # file size
    struct.pack_into('<Q', header, fp + 24 + 16 + 8 + 8, len(packets))  # data packets count
    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(data_obj)
        f.write(body)
        f.write(tail)
    return len(packets), total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--ffmpeg', default='C:/ffmpeg/ffmpeg.exe')
    ap.add_argument('--encoder', default='hevc_nvenc')
    ap.add_argument('--q', type=int, default=28)
    ap.add_argument('--gop', type=int, default=120)
    # 2026-11-20: a bitrate of its own for each movie, a share of the original's video bitrate (the narrations at 35 %:
    # PSNR 41.8 dB, SSIM 0.979 against the WMV3 picture). Constant quality (--q) made some narrations bigger than the
    # originals: these WMVs are low-bitrate already, and a quality target spends bits on their artefacts.
    ap.add_argument('--fraction', type=float, default=0.35, help='the share of the original video bitrate (0: --q)')
    a = ap.parse_args()
    src = Asf(open(a.src, 'rb').read())
    objs = src.media_objects()
    video = [o for o in objs if o['stream'] == src.video_stream]
    seconds = (max(o['time'] for o in video) - min(o['time'] for o in video)) / 1000 * len(video) / max(1, len(video) - 1)
    source_kbps = sum(len(o['data']) for o in video) * 8 / max(seconds, 0.001) / 1000
    with tempfile.TemporaryDirectory() as tmp:
        raw_path = os.path.join(tmp, 'v.hevc')
        if a.encoder == 'libx265':
            enc = ['-c:v', 'libx265', '-crf', str(a.q), '-preset', 'medium',
                   '-x265-params', f'bframes=0:aud=1:repeat-headers=1:keyint={a.gop}:min-keyint={a.gop}:log-level=error']
        elif a.fraction > 0:
            kbps = max(200, int(source_kbps * a.fraction))
            enc = ['-c:v', 'hevc_nvenc', '-bf', '0', '-aud', '1', '-g', str(a.gop), '-rc', 'vbr', '-b:v', f'{kbps}k',
                   '-maxrate', f'{kbps * 3 // 2}k', '-bufsize', f'{kbps * 2}k', '-preset', 'p7', '-tune', 'hq',
                   '-multipass', 'fullres', '-forced-idr', '1']
        else:
            enc = ['-c:v', 'hevc_nvenc', '-bf', '0', '-aud', '1', '-g', str(a.gop), '-rc', 'vbr', '-cq', str(a.q),
                   '-preset', 'p6', '-forced-idr', '1']
        subprocess.run([a.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', a.src, '-map', '0:v:0', *enc,
                        '-pix_fmt', 'yuv420p', '-fps_mode', 'passthrough', '-f', 'hevc', raw_path], check=True)
        aus = access_units(open(raw_path, 'rb').read())
    if len(aus) != len(video):
        raise SystemExit(f'{a.src}: {len(aus)} H.265 frames for {len(video)} WMV3 frames')
    for o, (au, key) in zip(video, aus):
        o['data'] = bytearray(au)
        o['key'] = key
    packets, total = write_asf(src, objs, a.dst)
    s = os.path.getsize(a.src)
    print(f'{os.path.basename(a.dst)}: {s / 1048576:.1f} MB -> {total / 1048576:.1f} MB ({100 * total / s:.1f} %), '
          f'{len(video)} frames ({sum(1 for _, k in aus if k)} keyframes), {packets} packets of {src.packet_size} bytes')


if __name__ == '__main__':
    main()
