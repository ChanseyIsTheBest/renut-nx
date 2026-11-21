# Smaller installs: packed files, Opus music, H.265 movies (2026-11-21)

The user (2026-11-21 23:06): "compress everything besides those 100 % compressed ones and re-encode all the audio and
video". This page holds what was measured on the disc, what each piece reuses from the other ports, and where each
stands. Game data is never committed, uploaded or distributed: everything here was measured on the PC's copy.

## The game's files (measured on the disc, D:\Banjo kazooie port\game)

| Files (first bytes) | Count | Size | What becomes of them |
|---|---|---|---|
| `Bundle/50/*` (43 8C B4 7C) | 151 | 3,285 MB | LZ4 HC 9: 2,439 MB (74.2 %); ZXC 7: 2,378 MB (72.4 %) |
| `Bundle/4f/*`, compressed already (0F F5 12 ED) | 142 | 1,339 MB | as they are (~100 % with either codec) |
| `Bundle/4f/*`, CAFF | 9 | 651 MB | LZ4: 331 MB (50.8 %); ZXC: 322 MB (49.5 %) |
| `Debug/**`, ASF movies (30 26 B2 75) | 52 | 1,211 MB | H.265 (below) |
| `Debug/**`, CAFF | 107 | 1.1 MB | LZ4: 41 % |
| `loctext/*`, CAFF | 1,498 | 13.8 MB | LZ4: 45 % |
| `xuifontcachefont/*` | 6 | 47 MB | LZ4: 95 % |
| default.xex, default.xexp, small files | | ~20 MB | as they are |

Packing every file but the movies, the compressed bundles and the xex: 1,785 files, 3,998 MB -> 2,822 MB with LZ4 HC 9
(1,177 MB smaller). ZXC gains ~70 MB more (Bundle/50 and the CAFF bundles): not worth a second format yet.

## The audio (Bundle/50/685374, 820 MB: 420 XACT wave banks)

XACT 3 wave banks (version 43, signature bytes 'DNBW', big-endian), inside the bundle file (the first at byte 97,148,
not aligned). Each sound a 24-byte entry: flags and duration, the format word, play region (offset in the bank's wave
data, length), loop region. The format word read big-endian keeps the PC's bit order (low bits first: tag 2 bits
(0 PCM, 1 XMA, 2 ADPCM), channels 3, rate 18, block align 8, bits 1); checked: XMA entries are whole 2048-byte packets,
PCM is big-endian 16-bit (neighbouring samples step a few hundred read big-endian, ~21,600 little-endian).

| Kind | Sounds | Size |
|---|---|---|
| PCM stereo 44.1 kHz (the music: MusicWorldofSport ...) | 15 | 507 MB |
| XMA mono (22 kHz mostly) | 8,761 | 70 MB |
| XMA stereo (44.1 kHz mostly) | 170 | 138 MB |
| XMA 4 channels | 1 | 0.4 MB |
| PCM mono | 16 | 1.5 MB |

The bank sasrt-nx's survey saw in Bundle/4f/3e5515 is not one: 'DNBW' bytes inside compressed data.

- **The music, NAME.opcm** (rr6-nx's format, sdk/include/rex/filesystem/opus_pcm.h; console-tested in rr6-nx
  2026-11-21): the file with its PCM ranges kept as 48 kHz Opus, every other byte stored. 507 MB of music at
  128 kbit/s is ~50 MB: about 450 MB smaller.
- **The XMA sounds** (215 MB): sasrt-nx's NAME.xmop (XMA frames from Opus inside the XMA emulation) would take ~100 MB
  more, but one file cannot be both .opcm and .xmop: it needs the two formats merged. Not started.

## The movies (Debug/**: 52 ASF files, WMV)

N&B's codegen names its functions, so the movie path is known by name (the generated code, app/generated):
CCalMoviePlayer (VideoDecoderThread, GetNextFrame, RenderNextFrame), CCalWmvDecoder (Create, Decode, GetWmvData, Seek,
Close, ConnectInput), the WMV9 codec's interface CodecCreate / CodecSubmit / CodecProcess / CodecFlush / CodecDestroy,
VodecConstruct (the FourCC check: WVC1 WMVA WMV3 WMV2 WMV1 WVP2 WMVP WMVR MP4S MP43 MP42), WmvideoC9DecNew / Init,
WMVideoDecInit, WMVideoDecDecodeSequenceHeader, and the ASF parser (ASFDecode*, ASFParse*, ASFLoad*). culdcept-nx's
app/src/cpu/culdcept_wmv_ffmpeg.cpp hooks the same codec interface in Culdcept Saga (by address there: no names): its
destroy / decode / get output / flush and GetData, decoding with FFmpeg, and an H.265 movie (the installer's re-encode,
kept in its ASF file) on NVDEC through tools/switch/nxvideo. The port for N&B: those hooks on the named functions and
N&B's structure offsets. Not started.

## Where each piece is

- **Installer** (banjo-nx-installer, local branch `media-pack`, not pushed): 1cf9c85 every game file that gets smaller
  as NAME.lz4b (LZ4 HC 9, 256 KB blocks; skipped by their first bytes: ASF, 0F F5 12 ED; default.xex; files under 4 KB),
  the zip's streamed entries, no Content-Length when packing (sw.js); fbe0170 the music as NAME.opcm (rr6-nx-installer's
  lib/opcm.js with an XACT wave bank finder; Node check: its 24 PCM sounds, 508 MB, found in ~100 ms).
- **Port**: the ultimate base's SDK reads version 1 .lz4b already (rr6-nx's first copy); renut-nx 1.0.2 reads none,
  so these installs need a release built on the ultimate base. rr6-nx's newer SDK filesystem pieces (ZXC .lz4b,
  .opcm with switch-libopus, the lookup order NAME.opcm before NAME before NAME.lz4b) are a superset of ours: the copy
  is scripted (scratchpad port_media_sdk.sh), applied after the render-thread batch's use build.
- **Tests to do**: an install made by the page (test/browser.html) run in Ryujinx from the packed files (loads, the
  music plays), then the console.

## First test (2026-11-22, Ryujinx, NRO a8e1aa3a with rr6-nx's readers)

The game folder packed as the installer packs it (tools/pack_game_root.py: 951 files 3,178 -> 2,071 MB as .lz4b) and
Bundle/50/685374 as NAME.opcm (tools/xwb_opcm_pack.py with ffmpeg's libopus: 819.5 -> 361.2 MB, the 24 PCM sounds as
Opus, the 15 music tracks ~4 MB each instead of ~41 MB): boot, title and Showdown Town as usual (the city at the
emulator's usual 13-16 fps), 0 errors; 53 [lz4b] reads logged; Bundle(85374 opened and read through its .opcm
(reads in the music region, ~500 MB in). Its open took 0.8-1.1 s (the .opcm's tables): worth trimming. Music at the title
comes from Bundlef85374 (9 MB, a compressed bundle: stays as it is).

**Caution, learned the hard way:** the emulator SD's game_root is a link to D:\Banjo kazooie port\game (the master
copy). pack_game_root.py packed the master in place for this test; everything was moved back with its restore (952
files, the classification identical to before) and the tool now refuses a linked root. Test packed installs in a
separate copy.

Still to do: the installer end to end in Chromium (test/browser.html) into a separate SD folder, the console (after the
user's word: held until the next render-thread round), the movies (H.265, the named CCalWmvDecoder / Codec* hooks).
