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

## The movies (Debug/**: 52 ASF files, WMV3)

All 52 are WMV3 1280x720 at 30 fps (43 at 30/1, 9 at 30000/1001), 19.1 minutes in all, audio WMA 2 / WMA Pro (some
5.1). culdcept-nx's movie path is used as it is (credit culdcept-nx; its method from sasrt-nx and tw2de_nx):

- **Port** (app/src/cpu/banjo_wmv_ffmpeg.cpp, culdcept-nx's culdcept_wmv_ffmpeg.cpp): Culdcept Saga links the same XMedia
  WMV9 wrapper (the same build: the same instructions but for registers, the same FourCC table), and N&B's codegen
  names it: decode = WmvideoC9DecDecode, get output = WmvideoC9DecGetOutput, flush = WmvideoC9DecReset, destroy =
  WmvideoC9DecClose, GetData = WMVDecCBGetData_HANDLER, the status = sub_82AA9070, the stream node's payload fetch
  (mode 2) = WMDecCBGetData (found by comparing the generated code of both games; the wrapper's offsets are the same).
  N&B's own Codec* functions are the voice chat's (XHV), not the movies'. banjo_wmv_ffmpeg: 0 the game's decoder, 1
  FFmpeg (WMV3) / nxvideo (H.265), 2 both compared. The SDK's FFmpeg has the WMV3 decoder on (config.h includes
  config_masseffect_wmv3.h, force-added as culdcept-nx did). nxvideo (tools/switch/nxvideo, culdcept-nx's): devkitPro's
  switch-ffmpeg 7.1 with nvtegra in one object (only nxv_* global): the H.265 movies on NVDEC, the VIC copy into a
  256-byte aligned NV12 picture, NVDEC / VIC at their maximum clocks while a movie plays (banjo_movie_max_clock: the
  video units' clocks, not the CPU's), the decode thread at 0x2C while it waits (banjo_movie_decode_priority).
- **Installer** (worker.js; culdcept-nx-installer's lib/asf.js, lib/movies.js, movie_worker.js, wasm/wmv3): each movie
  decoded (FFmpeg's WMV3 in WebAssembly) and encoded by the browser's H.265 encoder (WebCodecs, the computer's video
  hardware) at 35 % of its bitrate, a keyframe every 120 frames, written back into its ASF file in the original
  muxer's packet layout (culdcept-nx found the XDK player loses a split audio object's sound and paces by the packets'
  send times). A browser without an H.265 encoder keeps the WMVs.
- **Measured** (2026-11-22, test/movie_browser.html in Chromium, 2 workers): all 52 re-encoded, 1,155 -> 427 MiB
  (37 %), 177 s; none kept as WMV.
- Ryujinx decodes no H.265 on NVDEC (culdcept-nx): the H.265 movies' playback is a console test (held until the
  user's next render-thread round).

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
user's word: held until the next render-thread round). (Both done since but the console: the sections below.)

## The installer end to end (2026-11-22, Chromium: test/browser.html?compile=0&sink=1)

serve.py --test-sink reads the zip as it streams and writes only what the installer made (NAME.lz4b, NAME.opcm) into a
separate game_root (D:\New folder\ryu-data\banjo-ult\sdcard\switch\renut-nx\game_root_inst); test/sink_finish.py checks
every entry against the game folder and links the unchanged files in as hard links (the game folder only read; a
whole install would not fit the drive). The run: 1,383 s (the shaders' GLSL 723 s, identical to the release's
reference build); zip 5,332,559,140 bytes, 1,999 entries, end records good; 951 game files 3,031 -> 1,975 MiB as
.lz4b, every one decoded equal to its original byte for byte; the music 819.5 -> 357.9 MB as .opcm (the browser's
Opus, 105 s); 1,029 entries stored as they are, all equal; 0 bad. Then the 52 movies into the same folder as H.265
(test/movie_browser.html, above). Next: the folder in Ryujinx (the packed reads, the music, the movies' remux), the
WMV3 path compared with the game's decoder (banjo_wmv_ffmpeg 2) on the disc's movies.

## The movies in Ryujinx (2026-11-22, NRO 97b39427: out/builds/renut-nx_movies_97b39427)

Title screen, no input, 4-5 minutes (out/emu/emu_movie_*.toml; runs in out/emu_runs/emu_movie_*):

- **The disc's WMV3, banjo_wmv_ffmpeg 2** (the game's decoder shown, FFmpeg's compared): the two boot movies (265 and
  499 frames) decoded by both; all 80 compared frames (40 a movie, I and P) **bit-identical** (0 samples differ in Y, U
  and V), 0 failed, 0 damaged.
- **The disc's WMV3, banjo_wmv_ffmpeg 1** (FFmpeg's picture shown, the default): both movies played through (265 / 499
  frames, 0 failed, 0 damaged), the picture right. FFmpeg 32-49 ms a frame under Ryujinx's JIT (native on the console).
  A 15 s stall in the first movie was the render thread printing its 10 s report (Ryujinx's slow log writes; the
  whole game waited), not the movie path: logs-off builds print none.
- **The installer's folder** (game_root_inst swapped in: the packed files, the music as .opcm, the 52 movies as the
  browser's H.265): boot, title and the attract movie with 0 failed frames; the .lz4b reads (56 logged) and the .opcm
  (its open 0.48 s) as before. Ryujinx has no H.265 on NVDEC ("Nvdec Decode: Unsupported codec Hevc" in its own log):
  the first movie's 265 "NVDEC" frames were empty pictures nxvideo could not tell from real ones; the later movies
  went to nxvideo's software decoder (FFmpeg 7.1's HEVC: 20-53 ms a frame under the JIT) and played right: the
  picture clean (the browser's 35 % bitrate), each movie as long as the WMV's (499 frames in 18.5 s against 22-26.5 s
  for the WMV3 runs), so the remux's pacing holds (culdcept-nx's bug was slow, silent movies). The sound and NVDEC
  are for the console (held until the user's next render-thread round).

The test folder (game_root_inst) and out/media_test were deleted the same day for disk space (the user's word); to make
them again: serve.py --test-sink with test/browser.html?compile=0&sink=1 (~23 min), test/sink_finish.py --link, then
test/movie_browser.html (~3 min); tools/xwb_opcm_pack.py for a PC-made .opcm.

