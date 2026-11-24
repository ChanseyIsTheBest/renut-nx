// renut-nx (2026-11-22, the user: "re-encode all the audio and video"): Nuts & Bolts' movies (Debug/**: 52 ASF files,
// WMV3 1280x720 at 30 fps, 19 minutes) decoded on the host instead of by Microsoft's WMV9 decoder in guest code: FFmpeg's
// WMV3 decoder for the disc's movies, and nxvideo (tools/switch/nxvideo: NVDEC) for the H.265 movies the installer makes
// (the WMV3 video re-encoded frame for frame inside the same ASF file, its audio, timing and sequence header kept).
// culdcept-nx's src/cpu/culdcept_wmv_ffmpeg.cpp and tools/switch/nxvideo, as they are (credit culdcept-nx; its method
// from sasrt-nx's sasrt_usm_ffmpeg.cpp and tw2de_nx's tw2de_usm_ffmpeg.cpp, credit both). Culdcept Saga links the same
// XMedia WMV9 wrapper (the same build: the same instructions but for registers, the same FourCCs); N&B's codegen names
// it, so the hooks take N&B's names for Culdcept's addresses:
//   decode      sub_8259EE00 -> WmvideoC9DecDecode        get output  sub_8259EE60 -> WmvideoC9DecGetOutput
//   flush       sub_8259ECF8 -> WmvideoC9DecReset         destroy     sub_8259ED50 -> WmvideoC9DecClose
//   GetData     sub_8259EA38 -> WMVDecCBGetData_HANDLER   status      sub_8259E960 -> sub_82AA9070
//   the stream node's payload fetch (mode 2)  sub_8256FEE8 -> WMDecCBGetData
// The wrapper's layout is the same (C+32 V; V+32 width, +36 height, +68 the WMV9 decoder, +72 pts, +84 / +88 the
// sequence header, +108 the frame's I420 size): checked in both games' generated code.
//
// culdcept-nx's notes, kept for the layout (Culdcept's addresses):
// culdcept-nx (2026-11-19, the user: "can you do the wmv decoder now please", "the native one"; the method of sasrt-nx's
// src/cpu/sasrt_usm_ffmpeg.cpp and tw2de_nx's tw2de_usm_ffmpeg.cpp, credit both; the guest's movie player mapped by the
// sasrt-nx session's read-only notes of the same day): Culdcept Saga's movies (data\MOVIE\*.wmv: ASF, WMV3 1280x720 at 30
// or 60 fps, no B pictures) decoded by FFmpeg on the host instead of Microsoft's WMV9 decoder in guest code (on the
// console the guest decoder took ~97 % of a core while a movie played, which presented at ~48 fps).
//
// The guest codec's interface C (44 bytes, filled by sub_8259F3E0): +4 destroy sub_8259ED50(C), +8 decode
// sub_8259EE00(C, u32* pcb), +12 get output sub_8259EE60(C, buf, size, u32* pcb, u32*, u64* pts, u32* flag), +16 flush
// sub_8259ECF8(C), +32 its wrapper V (+32 width, +36 height, +68 the WMV9 decoder, +72 the frame's pts, +84 / +88 the
// sequence header and its length, +108 the frame's I420 size), +40 GetData sub_8259EA38(C, offset, u8** p, min, u32* n,
// u32* not_end): the frame's ASF payload a chunk at a time, status 10 and not_end 0 with its last. All five are reached
// only through tables (no direct callers), so the hooks below take every call.
//
// culdcept_wmv_ffmpeg: 0 the game's decoder; 1 FFmpeg: the frame's chunks taken through GetData as the guest's decoder
// takes them (the pts kept as it keeps it), decoded, and written as I420 into the player's buffer by get output; 2 both:
// the game's decoder runs and its picture is shown, FFmpeg decodes the same bytes (taken as the stream node hands them
// out, sub_8256FEE8) and the planes are compared ([wmv] lines).
//
// 2026-11-20 (the user: "I think getting hardware h.265 decoding would br good in thr long run ... lets do that"): a movie
// whose first frame is H.265 (the installer re-encodes the WMV3 video, frame for frame, keeping the ASF file, its audio,
// timing and the WMV3 stream's sequence header the game's player sets up from) is decoded by nxvideo
// (tools/switch/nxvideo: devkitPro's FFmpeg 7.1 with averne's nvtegra backend, on the Switch's NVDEC; its software
// HEVC decoder where NVDEC does not open). culdcept_movie_nvdec: 0 software, 1 H.265 on NVDEC, 2 WMV3 too.
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>
#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if RENUT_NXVIDEO
#include <switch.h>  // svcGetThreadPriority / svcSetThreadPriority (banjo_movie_decode_priority)

#include "nxvideo.h"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
}

REXCVAR_DEFINE_INT32(banjo_wmv_ffmpeg, 1, "Banjo",
                     "The movies' WMV3 video: 0 the game's decoder (guest code), 1 FFmpeg on the host, 2 both, compared "
                     "([wmv] log lines; the game's picture is shown)")
    .range(0, 2);
REXCVAR_DEFINE_INT32(banjo_movie_nvdec, 1, "Banjo",
                     "The movies on the Switch's hardware decoder (NVDEC, through nxvideo): 0 none (software), 1 the "
                     "H.265 movies the installer made, 2 the game's WMV3 movies too")
    .range(0, 2);
// 2026-11-20 (the user on the console: "audio doesn't play during the video and the video has major stutters"): the
// H.265 narrations decoded at 21-25 ms a frame in the game (60 fps needs 16.7) where the console's benchmark, alone,
// took 4.5 ms on NVDEC. The decode waits on the hardware, on the game's thread at its priority (0x3B, the lowest):
// woken, it waits for a core behind the game's other threads, which Horizon does not time-slice. nxv_decode runs at
// this priority (above the game's threads) and the thread's own is put back after.
REXCVAR_DEFINE_INT32(banjo_movie_decode_priority, 0x2C, "Banjo",
                     "The thread's priority while it waits for an H.265 frame on NVDEC (0x1C-0x3B, lower runs first; "
                     "the game's threads 0x3B, the audio threads 0x2A-0x2D); 0 = the thread's own")
    .range(0, 0x3B);
// 2026-11-20: nvtegra's DFS sets NVDEC's clock to the rate frames come at, and the game asks for a frame only after the
// last is shown: the clock fell until a frame took a whole display frame (tools/switch/nxvideo/nxvideo.c).
REXCVAR_DEFINE_BOOL(banjo_movie_max_clock, true, "Banjo",
                    "While an H.265 movie plays on NVDEC: NVDEC at the clock nvtegra's DFS starts from (its maximum) "
                    "and the VIC at its maximum. false: DFS's own clock (it slows the movies down in this game)");
// 2026-11-21 (the user on the console: "audio plays during video properly now but video drops frames"): 19.5 ms a
// frame in the narrations, 18 of it copying the picture out of NVDEC's surface: FFmpeg's nvtegra did it with the CPU
// (its fallback for a picture that is not 256-byte aligned). nxvideo now gives it an aligned picture of its own, which
// the VIC fills (tools/switch/nxvideo/nxvideo.c).
REXCVAR_DEFINE_BOOL(banjo_movie_vic_copy, true, "Banjo",
                    "H.265 movies on NVDEC: the picture copied out of NVDEC's surface by the VIC into an aligned NV12 "
                    "picture, the same bytes as the CPU's copy (false: FFmpeg's own picture, copied by the CPU: 18 ms a "
                    "frame in the game)");
REXCVAR_DEFINE_BOOL(banjo_movie_verify, false, "Banjo",
                    "H.265 movies on NVDEC: every 30th VIC picture compared with FFmpeg's CPU copy of it (the log's "
                    "nxvideo line; each comparison costs a CPU copy, about 18 ms: a test, not for playing)");
REXCVAR_DEFINE_STRING(banjo_movie_verify_dump, "", "Banjo",
                      "With banjo_movie_verify: a folder the first compared pair of pictures is written to "
                      "(I420), e.g. sdmc:/switch/renut-nx/logs");

REX_EXTERN(__imp__rex_WmvideoC9DecDecode);  // decode (C, u32* pcb)
REX_EXTERN(__imp__rex_WmvideoC9DecGetOutput);  // get output (C, buf, size, u32* pcb, u32*, u64* pts, u32* flag)
REX_EXTERN(__imp__rex_WmvideoC9DecReset);  // flush (C)
REX_EXTERN(__imp__rex_WmvideoC9DecClose);  // destroy (C)
REX_EXTERN(__imp__rex_WMVDecCBGetData_HANDLER);  // GetData (C, offset, u8** p, min, u32* n, u32* not_end)
REX_EXTERN(__imp__rex_WMDecCBGetData);  // the stream node's payload fetch (S, u8** p, u32* n, ...)
REX_EXTERN(__imp__sub_82AA9070);  // the decoder's status to an HRESULT

namespace {

uint32_t Ld32(const uint8_t *base, uint32_t va) {
  uint32_t v;
  std::memcpy(&v, base + va, 4);
  return __builtin_bswap32(v);
}
uint16_t Ld16(const uint8_t *base, uint32_t va) {
  uint16_t v;
  std::memcpy(&v, base + va, 2);
  return __builtin_bswap16(v);
}
uint64_t Ld64(const uint8_t *base, uint32_t va) {
  uint64_t v;
  std::memcpy(&v, base + va, 8);
  return __builtin_bswap64(v);
}
void St32(uint8_t *base, uint32_t va, uint32_t v) {
  if (!va)
    return;
  v = __builtin_bswap32(v);
  std::memcpy(base + va, &v, 4);
  rex::memory::MarkStoreCardsAt(base, va, 4);
}
void St64(uint8_t *base, uint32_t va, uint64_t v) {
  if (!va)
    return;
  v = __builtin_bswap64(v);
  std::memcpy(base + va, &v, 8);
  rex::memory::MarkStoreCardsAt(base, va, 8);
}

constexpr uint32_t kV = 32;             // C+32: the wrapper V
constexpr uint32_t kWidth = 32;         // V+32
constexpr uint32_t kHeight = 36;        // V+36
constexpr uint32_t kPts = 72;           // V+72 (u64)
constexpr uint32_t kSequence = 84;      // V+84: the sequence header (ASF stream properties' extra bytes)
constexpr uint32_t kSequenceLen = 88;   // V+88 (u16)
constexpr uint32_t kFrameSize = 108;    // V+108: width * height * 3 / 2
constexpr uint32_t kStatusOk = 0, kStatusNoMoreOutput = 9;

struct Movie {
  std::mutex mutex;
  AVCodecContext *avctx = nullptr;
  AVPacket *packet = nullptr;
  AVFrame *frame = nullptr;
  bool have = false;               // a picture decoded and not yet taken by get output
  std::vector<uint8_t> bytes;      // this frame's
  std::vector<uint8_t> captured;   // mode 2: what the guest's decoder took
  uint64_t frames = 0, decoded = 0, failed = 0, damaged = 0, compared = 0;
  double decode_ms = 0.0, copy_ms = 0.0;
  double nxv_ms = 0.0;  // of decode_ms, in nxv_decode (2026-11-20)
  uint64_t timed = 0;
  std::chrono::steady_clock::time_point since{};
  uint32_t width = 0, height = 0;
  // 2026-11-20: nxvideo (H.265 always; WMV3 with banjo_movie_nvdec 2)
  bool checked = false;  // the first frame looked at
  bool hevc = false;     // the movie's video is H.265
  bool nxv_used = false;
#if RENUT_NXVIDEO
  NxvDecoder *nxv = nullptr;
  NxvPicture pic{};
#endif
};

// An H.265 access unit in Annex B: a start code, then a VPS (32), SPS (33), PPS (34) or access unit delimiter (35). A
// WMV3 frame (simple / main profile) carries no start codes.
bool LooksHevc(const std::vector<uint8_t> &b) {
  size_t i = 0;
  if (b.size() >= 6 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1)
    i = 4;
  else if (b.size() >= 5 && b[0] == 0 && b[1] == 0 && b[2] == 1)
    i = 3;
  else
    return false;
  const uint8_t type = (b[i] >> 1) & 0x3F;
  return (b[i] & 0x80) == 0 && type >= 32 && type <= 35 && b[i + 1] == 1;  // forbidden bit 0, layer 0, temporal id 1
}

std::mutex g_movies_mutex;
std::unordered_map<uint32_t, Movie *> g_movies;  // by the guest codec's address

Movie &MovieFor(uint32_t c) {
  std::lock_guard lock(g_movies_mutex);
  Movie *&m = g_movies[c];
  if (!m)
    m = new Movie();
  return *m;
}

void CloseDecoder(Movie &m) {
#if RENUT_NXVIDEO
  if (m.nxv)
    nxv_close(m.nxv);
  m.nxv = nullptr;
#endif
  m.checked = m.hevc = m.nxv_used = false;
  if (m.frame)
    av_frame_free(&m.frame);
  if (m.packet)
    av_packet_free(&m.packet);
  if (m.avctx)
    avcodec_free_context(&m.avctx);
  m.have = false;
}

// FFmpeg's WMV3 decoder for this movie: the sequence header the guest keeps (V+84, 4 or 5 bytes), its size.
bool OpenDecoder(Movie &m, const uint8_t *base, uint32_t v) {
  if (m.avctx)
    return true;
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_WMV3);
  if (!codec) {
    static std::atomic<bool> said{false};
    if (!said.exchange(true))
      REXLOG_WARN("[wmv] FFmpeg has no WMV3 decoder in this build: the game's decoder plays the movies");
    return false;
  }
  m.avctx = avcodec_alloc_context3(codec);
  m.width = Ld32(base, v + kWidth);
  m.height = Ld32(base, v + kHeight);
  m.avctx->width = int(m.width);
  m.avctx->height = int(m.height);
  m.avctx->coded_width = int(m.width);
  m.avctx->coded_height = int(m.height);
  m.avctx->flags |= AV_CODEC_FLAG_LOW_DELAY;  // each picture out as it is decoded (no B pictures in these movies)
  m.avctx->thread_count = 1;
  const uint32_t seq = Ld32(base, v + kSequence), seq_len = Ld16(base, v + kSequenceLen);
  if (seq && seq_len && seq_len < 64) {
    m.avctx->extradata = static_cast<uint8_t *>(av_mallocz(seq_len + AV_INPUT_BUFFER_PADDING_SIZE));
    std::memcpy(m.avctx->extradata, base + seq, seq_len);
    m.avctx->extradata_size = int(seq_len);
  }
  if (avcodec_open2(m.avctx, codec, nullptr) < 0) {
    REXLOG_WARN("[wmv] FFmpeg's WMV3 decoder did not open ({}x{}, sequence header of {} bytes)", m.width, m.height,
                seq_len);
    avcodec_free_context(&m.avctx);
    return false;
  }
  m.packet = av_packet_alloc();
  m.frame = av_frame_alloc();
  std::string hex;
  for (uint32_t i = 0; i < seq_len && i < 8; ++i)
    hex += fmt::format("{:02X} ", base[seq + i]);
  REXLOG_INFO("[wmv] FFmpeg's WMV3 decoder for a {}x{} movie (sequence header {}; banjo_wmv_ffmpeg {})", m.width,
              m.height, hex, REXCVAR_GET(banjo_wmv_ffmpeg));
  return true;
}

// One frame's bytes through FFmpeg: m.have and m.frame.
// nxvideo for this movie (H.265, or WMV3 with banjo_movie_nvdec 2): false when it is not used.
bool NxvDecode(Movie &m, const uint8_t *base, uint32_t v, const std::vector<uint8_t> &bytes) {
  if (!m.checked) {
    m.checked = true;
    m.hevc = LooksHevc(bytes);
    const int nvdec = REXCVAR_GET(banjo_movie_nvdec);
    m.nxv_used = m.hevc || nvdec >= 2;
#if RENUT_NXVIDEO
    if (m.nxv_used) {
      m.width = Ld32(base, v + kWidth);
      m.height = Ld32(base, v + kHeight);
      const uint32_t seq = Ld32(base, v + kSequence), seq_len = Ld16(base, v + kSequenceLen);
      const bool wmv3 = !m.hevc;
      char why[160] = {};
      nxv_options(REXCVAR_GET(banjo_movie_max_clock) ? 1 : 0, REXCVAR_GET(banjo_movie_vic_copy) ? 1 : 0,
                  REXCVAR_GET(banjo_movie_verify) ? 1 : 0, REXCVAR_GET(banjo_movie_verify_dump).c_str());
      m.nxv = nxv_open(m.hevc ? NXV_HEVC : NXV_WMV3, wmv3 && seq && seq_len < 64 ? base + seq : nullptr,
                       wmv3 ? int(seq_len) : 0, int(m.width), int(m.height), m.hevc ? nvdec >= 1 : 1, why, sizeof(why));
      REXLOG_INFO("[wmv] {}: {}", m.hevc ? "an H.265 movie" : "a WMV3 movie on nxvideo", m.nxv ? why : "did not open");
      if (!m.nxv && !m.hevc)
        m.nxv_used = false;  // WMV3: the 4.4 path as before
    }
#else
    if (m.hevc)
      REXLOG_WARN("[wmv] an H.265 movie, and this build has no H.265 decoder (nxvideo is the Switch's): not shown");
    m.nxv_used = m.hevc;
#endif
  }
  if (!m.nxv_used)
    return false;
#if RENUT_NXVIDEO
  const int boost = REXCVAR_GET(banjo_movie_decode_priority);
  s32 own = 0;
  const bool raised = boost > 0 && R_SUCCEEDED(svcGetThreadPriority(&own, CUR_THREAD_HANDLE)) && own > boost &&
                      R_SUCCEEDED(svcSetThreadPriority(CUR_THREAD_HANDLE, u32(boost)));
  const auto t0 = std::chrono::steady_clock::now();
  const int r = m.nxv ? nxv_decode(m.nxv, bytes.data(), int(bytes.size()), &m.pic) : -1;
  m.nxv_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (raised)
    svcSetThreadPriority(CUR_THREAD_HANDLE, u32(own));
  m.have = r == 1;
#else
  m.have = false;
#endif
  if (m.have)
    ++m.decoded;
  else if (++m.failed <= 8)
    REXLOG_WARN("[wmv] nxvideo gave no picture for frame {} ({} bytes)", m.frames, bytes.size());
  return true;
}

void Decode(Movie &m, const uint8_t *base, uint32_t v, const std::vector<uint8_t> &bytes) {
  m.have = false;
  ++m.frames;
  if (!bytes.empty() && NxvDecode(m, base, v, bytes))
    return;
  if (bytes.empty() || !OpenDecoder(m, base, v)) {
    ++m.failed;
    return;
  }
  av_packet_unref(m.packet);
  if (av_new_packet(m.packet, int(bytes.size())) < 0)
    return;
  std::memcpy(m.packet->data, bytes.data(), bytes.size());
  if (avcodec_send_packet(m.avctx, m.packet) >= 0)
    m.have = avcodec_receive_frame(m.avctx, m.frame) >= 0;
  if (!m.have) {
    if (++m.failed <= 8)
      REXLOG_WARN("[wmv] FFmpeg gave no picture for frame {} ({} bytes)", m.frames, bytes.size());
    return;
  }
  ++m.decoded;
  if (m.frame->decode_error_flags || (m.frame->flags & AV_FRAME_FLAG_CORRUPT))
    ++m.damaged;
}

// The frame's chunks through the guest's GetData (C, 0, &p, 4, &n, &not_end) until its last, as the guest's decoder
// takes them: the pts (V+72) and the payload's release stay the guest's. Out-arguments in a scratch area below the stack.
void TakeFrame(PPCContext &ctx, uint8_t *base, uint32_t c, std::vector<uint8_t> &bytes) {
  bytes.clear();
  const uint32_t r1 = ctx.r1.u32;
  const uint32_t frame = r1 - 128;
  const uint32_t p_out = frame + 80, n_out = frame + 84, end_out = frame + 88;
  for (int i = 0; i < 4096; ++i) {
    ctx.r1.u64 = frame;
    ctx.r3.u64 = c;
    ctx.r4.u64 = 0;
    ctx.r5.u64 = p_out;
    ctx.r6.u64 = 4;
    ctx.r7.u64 = n_out;
    ctx.r8.u64 = end_out;
    __imp__rex_WMVDecCBGetData_HANDLER(ctx, base);
    const uint32_t status = ctx.r3.u32;
    const uint32_t p = Ld32(base, p_out), n = Ld32(base, n_out), not_end = Ld32(base, end_out);
    if (p && n)
      bytes.insert(bytes.end(), base + p, base + p + n);
    if ((status != 0 && status != 10) || !n || !not_end)
      break;
  }
  ctx.r1.u64 = r1;
}

// FFmpeg's picture (YUV420P) into the player's I420 buffer: Y pitch W, then U and V pitch W / 2.
void WriteI420(Movie &m, uint8_t *base, uint32_t buf, uint32_t w, uint32_t h) {
  const AVFrame *f = m.frame;
  const uint32_t fw = std::min<uint32_t>(w, uint32_t(f->width)), fh = std::min<uint32_t>(h, uint32_t(f->height));
  const uint32_t planes[3] = {buf, buf + w * h, buf + w * h + (w / 2) * (h / 2)};
  for (int k = 0; k < 3; ++k) {
    const uint32_t pw = k ? fw / 2 : fw, ph = k ? fh / 2 : fh, pitch = k ? w / 2 : w;
    for (uint32_t y = 0; y < ph; ++y)
      std::memcpy(base + planes[k] + y * pitch, f->data[k] + size_t(y) * size_t(f->linesize[k]), pw);
  }
  rex::memory::MarkStoreCardsAt(base, buf, w * h * 3 / 2);
}

#if RENUT_NXVIDEO
// nxvideo's picture (YUV420P, or NV12 from NVDEC) into the player's I420 buffer.
void WriteI420Nxv(const NxvPicture &p, uint8_t *base, uint32_t buf, uint32_t w, uint32_t h) {
  const uint32_t fw = std::min<uint32_t>(w, uint32_t(p.width)), fh = std::min<uint32_t>(h, uint32_t(p.height));
  const uint32_t y_plane = buf, u_plane = buf + w * h, v_plane = buf + w * h + (w / 2) * (h / 2);
  for (uint32_t y = 0; y < fh; ++y)
    std::memcpy(base + y_plane + y * w, p.data[0] + size_t(y) * size_t(p.linesize[0]), fw);
  if (p.nv12) {
    for (uint32_t y = 0; y < fh / 2; ++y) {
      const uint8_t *uv = p.data[1] + size_t(y) * size_t(p.linesize[1]);
      uint8_t *u = base + u_plane + y * (w / 2), *vv = base + v_plane + y * (w / 2);
      for (uint32_t x = 0; x < fw / 2; ++x) {
        u[x] = uv[2 * x];
        vv[x] = uv[2 * x + 1];
      }
    }
  } else {
    for (uint32_t y = 0; y < fh / 2; ++y) {
      std::memcpy(base + u_plane + y * (w / 2), p.data[1] + size_t(y) * size_t(p.linesize[1]), fw / 2);
      std::memcpy(base + v_plane + y * (w / 2), p.data[2] + size_t(y) * size_t(p.linesize[2]), fw / 2);
    }
  }
  rex::memory::MarkStoreCardsAt(base, buf, w * h * 3 / 2);
}
#endif

#if RENUT_NXVIDEO
// nxvideo's times since its last line (then cleared): every 10 s and at the movie's close (2026-11-21: the intros are
// shorter than 10 s).
void NxvLine(Movie &m, uint32_t c) {
  if (!m.nxv) return;
  NxvStats st;
  nxv_stats(m.nxv, &st);
  if (!st.frames) return;
  const double n = double(st.frames);
  REXLOG_INFO("[wmv] {:08X}: nxvideo a frame ({}): send {:.2f} ms, receive {:.2f} ms, transfer {:.2f} ms; NVDEC {} MHz "
              "(DFS's start {} MHz, put back {} times), VIC {} MHz; copied out by the VIC {}, by the CPU {}{}",
              c, st.frames, st.send_ms / n, st.receive_ms / n, st.transfer_ms / n, st.nvdec_hz / 1000000,
              st.nvdec_max_hz / 1000000, st.restores, st.vic_hz / 1000000, st.vic_frames, st.cpu_frames,
              st.checked ? fmt::format("; verify: {} pictures compared with the CPU's copy, {} {} ({} samples)",
                                       st.checked, st.checked_differ, st.checked_differ ? "DIFFERENT" : "different",
                                       st.checked_samples)
                         : std::string());
}
#endif

void TimeLine(Movie &m, uint32_t c, double decode_ms, double copy_ms) {
  const auto now = std::chrono::steady_clock::now();
  m.decode_ms += decode_ms;
  m.copy_ms += copy_ms;
  ++m.timed;
  if (m.since.time_since_epoch().count() == 0)
    m.since = now;
  if (now - m.since >= std::chrono::seconds(10)) {
    REXLOG_INFO("[wmv] {:08X}: {} frames by FFmpeg in {:.1f} s ({}x{}): {:.2f} ms decoding ({:.2f} of it in nxvideo) "
                "and {:.2f} ms copying a frame; {} decoded, {} not, {} damaged so far",
                c, m.timed, std::chrono::duration<double>(now - m.since).count(), m.width, m.height,
                m.decode_ms / double(m.timed), m.nxv_ms / double(m.timed), m.copy_ms / double(m.timed), m.decoded,
                m.failed, m.damaged);
    m.decode_ms = m.copy_ms = m.nxv_ms = 0.0;
#if RENUT_NXVIDEO
    NxvLine(m, c);
#endif
    m.timed = 0;
    m.since = now;
  }
}

thread_local Movie *t_capture = nullptr;  // mode 2: the movie whose decode call is running on this thread

}  // namespace

// Decode: one frame.
REX_HOOK_RAW(rex_WmvideoC9DecDecode) {
  const int mode = REXCVAR_GET(banjo_wmv_ffmpeg);
  if (mode == 0) {
    __imp__rex_WmvideoC9DecDecode(ctx, base);
    return;
  }
  const uint32_t c = ctx.r3.u32, pcb = ctx.r4.u32;
  const uint32_t v = Ld32(base, c + kV);
  Movie &m = MovieFor(c);
  std::lock_guard lock(m.mutex);
  static std::atomic<bool> said{false};
  if (!said.exchange(true))
    REXLOG_INFO("[wmv] the movies' video: {} (banjo_wmv_ffmpeg {})",
                mode == 1 ? "FFmpeg on the host" : "the game's decoder and FFmpeg, compared", mode);
  if (mode == 2) {
    m.captured.clear();
    t_capture = &m;
    __imp__rex_WmvideoC9DecDecode(ctx, base);  // the game's decoder; its payload fetches captured
    t_capture = nullptr;
    const uint32_t result = ctx.r3.u32;
    Decode(m, base, v, m.captured);
    ctx.r3.u64 = result;
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  TakeFrame(ctx, base, c, m.bytes);
  if (m.bytes.empty()) {
    // Nothing more in the stream: the game's decoder answers as it would (it finds no bytes either).
    ctx.r3.u64 = c;
    ctx.r4.u64 = pcb;
    __imp__rex_WmvideoC9DecDecode(ctx, base);
    return;
  }
  Decode(m, base, v, m.bytes);
  const auto t1 = std::chrono::steady_clock::now();
  St32(base, pcb, m.have ? Ld32(base, v + kFrameSize) : 0);
  ctx.r3.u64 = m.have ? kStatusOk : kStatusNoMoreOutput;
  __imp__sub_82AA9070(ctx, base);
  if (m.have)
    TimeLine(m, c, std::chrono::duration<double, std::milli>(t1 - t0).count(), 0.0);
}

// Get output: the decoded frame into the player's buffer (I420).
REX_HOOK_RAW(rex_WmvideoC9DecGetOutput) {
  const int mode = REXCVAR_GET(banjo_wmv_ffmpeg);
  if (mode == 0) {
    __imp__rex_WmvideoC9DecGetOutput(ctx, base);
    return;
  }
  const uint32_t c = ctx.r3.u32, buf = ctx.r4.u32, size = ctx.r5.u32, pcb = ctx.r6.u32, p7 = ctx.r7.u32,
                 pts = ctx.r8.u32, flag = ctx.r9.u32;
  const uint32_t v = Ld32(base, c + kV);
  Movie &m = MovieFor(c);
  std::lock_guard lock(m.mutex);
  const uint32_t w = Ld32(base, v + kWidth), h = Ld32(base, v + kHeight);
  if (mode == 2) {
    __imp__rex_WmvideoC9DecGetOutput(ctx, base);  // the game's picture into buf
    const uint32_t result = ctx.r3.u32;
    if (m.have && m.compared < 40 && Ld32(base, pcb) != 0) {
      ++m.compared;
      const AVFrame *f = m.frame;
      const uint32_t planes[3] = {buf, buf + w * h, buf + w * h + (w / 2) * (h / 2)};
      std::string line;
      uint64_t differing = 0;
      for (int k = 0; k < 3; ++k) {
        const uint32_t pw = k ? w / 2 : w, ph = k ? h / 2 : h, pitch = pw;
        uint64_t sum = 0, n = 0;
        uint32_t worst = 0;
        for (uint32_t y = 0; y < ph && y < uint32_t(k ? f->height / 2 : f->height); ++y)
          for (uint32_t x = 0; x < pw && x < uint32_t(k ? f->width / 2 : f->width); ++x) {
            const uint8_t a = base[planes[k] + y * pitch + x], b = f->data[k][size_t(y) * size_t(f->linesize[k]) + x];
            const uint32_t d = a > b ? a - b : b - a;
            sum += d;
            differing += d != 0;
            worst = std::max(worst, d);
            ++n;
          }
        line += fmt::format(" {} mean {:.3f} worst {}", "YUV"[k], n ? double(sum) / double(n) : 0.0, worst);
      }
      REXLOG_INFO("[wmv] {:08X} frame {} (type {}) {}x{}: game vs FFmpeg:{}; {} samples differ", c, m.frames,
                  av_get_picture_type_char(f->pict_type), w, h, line, differing);
    }
    m.have = false;
    ctx.r3.u64 = result;
    return;
  }
  const uint32_t frame_size = Ld32(base, v + kFrameSize);
  if (frame_size > size || (h & 1)) {  // as the game's: E_ too small / odd height
    ctx.r3.u64 = 0x80500006u;
    return;
  }
  uint32_t status = kStatusNoMoreOutput;
  if (m.nxv_used) {
#if RENUT_NXVIDEO
    if (m.have) {
      const auto t0 = std::chrono::steady_clock::now();
      WriteI420Nxv(m.pic, base, buf, w, h);
      m.copy_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      St32(base, pcb, frame_size);
      status = kStatusOk;
    } else
#endif
    {
      St32(base, pcb, 0);
    }
    St32(base, p7, 0);
  } else if (m.have && m.frame->format == AV_PIX_FMT_YUV420P) {
    const auto t0 = std::chrono::steady_clock::now();
    WriteI420(m, base, buf, w, h);
    m.copy_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    St32(base, pcb, frame_size);
    St32(base, p7, 0);
    status = kStatusOk;
  } else {
    if (m.have && m.frame->format != AV_PIX_FMT_YUV420P) {
      static std::atomic<int> said{0};
      if (said.fetch_add(1) < 4)
        REXLOG_WARN("[wmv] FFmpeg's picture is in pixel format {}, not YUV420P: not shown", int(m.frame->format));
    }
    St32(base, pcb, 0);
    St32(base, p7, 0);
  }
  m.have = false;
  St64(base, pts, Ld64(base, v + kPts));
  St32(base, flag, 1);
  ctx.r3.u64 = status;
  __imp__sub_82AA9070(ctx, base);
}

// Flush (a seek, a stop): FFmpeg's references go too.
REX_HOOK_RAW(rex_WmvideoC9DecReset) {
  const uint32_t c = ctx.r3.u32;
  {
    Movie &m = MovieFor(c);
    std::lock_guard lock(m.mutex);
    if (m.avctx)
      avcodec_flush_buffers(m.avctx);
#if RENUT_NXVIDEO
    if (m.nxv)
      nxv_flush(m.nxv);
#endif
    m.have = false;
  }
  __imp__rex_WmvideoC9DecReset(ctx, base);
}

// Destroy: the movie's FFmpeg decoder freed (the next movie's codec may take the same address).
REX_HOOK_RAW(rex_WmvideoC9DecClose) {
  const uint32_t c = ctx.r3.u32;
  {
    Movie &m = MovieFor(c);
    std::lock_guard lock(m.mutex);
    if ((m.avctx || m.nxv_used) && (m.frames || m.failed)) {
#if RENUT_NXVIDEO
      const char *by = m.nxv_used ? (m.nxv && nxv_is_hw(m.nxv) ? "NVDEC" : "nxvideo (software)") : "FFmpeg";
#else
      const char *by = m.nxv_used ? "nothing (no H.265 decoder)" : "FFmpeg";
#endif
      REXLOG_INFO("[wmv] {:08X}: movie closed{}: {} frames, {} decoded by {}, {} not, {} damaged", c,
                  m.hevc ? " (H.265)" : "", m.frames, m.decoded, by, m.failed, m.damaged);
#if RENUT_NXVIDEO
      NxvLine(m, c);
#endif
    }
    CloseDecoder(m);
    m.frames = m.decoded = m.failed = m.damaged = m.compared = 0;
    m.timed = 0;
    m.since = {};
  }
  __imp__rex_WmvideoC9DecClose(ctx, base);
}

// Mode 2: the payload fragments the stream node hands the game's decoder, taken as they go (the video decode thread's
// decode call only: t_capture).
REX_HOOK_RAW(rex_WMDecCBGetData) {
  Movie *m = t_capture;
  const uint32_t pp = ctx.r4.u32, pn = ctx.r5.u32;
  __imp__rex_WMDecCBGetData(ctx, base);
  if (m && int32_t(ctx.r3.u32) >= 0 && pp && pn) {
    const uint32_t p = Ld32(base, pp), n = Ld32(base, pn);
    if (p && n && n < (16u << 20))
      m->captured.insert(m->captured.end(), base + p, base + p + n);
  }
}
