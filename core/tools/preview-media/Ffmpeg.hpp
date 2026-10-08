#pragma once

// FFmpeg plumbing shared by the media helper commands: RAII handles, error mapping, the deadline interrupt,
// the range-pull AVIOContext (input) and the hold-back AVIOContext (fragmented MP4 output to stdout).

#include "common/protocol.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vh::media {

// ── limits ────────────────────────────────────────────────────────────────────────────────────────────────
constexpr int kMaxDimension = 16384;
constexpr int64_t kMaxPixels = 8192LL * 8192LL;
constexpr unsigned kMaxStreams = 64;
constexpr int kMaxChannels = 32;
constexpr int kMaxSampleRate = 768000;
constexpr int64_t kProbeSize = 16LL << 20;
constexpr int64_t kAnalyzeDurationUs = 15LL * AV_TIME_BASE;

// ── RAII ──────────────────────────────────────────────────────────────────────────────────────────────────
struct CodecContextDeleter { void operator()(AVCodecContext* c) const { avcodec_free_context(&c); } };
struct FrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwsDeleter { void operator()(SwsContext* s) const { sws_freeContext(s); } };
struct SwrDeleter { void operator()(SwrContext* s) const { swr_free(&s); } };
struct BufferRefDeleter { void operator()(AVBufferRef* b) const { av_buffer_unref(&b); } };

using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;
using BufferRefPtr = std::unique_ptr<AVBufferRef, BufferRefDeleter>;

FramePtr makeFrame();
PacketPtr makePacket();

std::string avError(int err);

// ── options ───────────────────────────────────────────────────────────────────────────────────────────────
std::string option(const helpers::Args& args, const std::string& key, std::string fallback);
double numberOption(const helpers::Args& args, const std::string& key, double fallback, double min, double max);

// ── deadline ──────────────────────────────────────────────────────────────────────────────────────────────
class Deadline {
public:
    explicit Deadline(std::chrono::seconds budget);
    [[nodiscard]] bool expired() const;
    void check() const;  // throws LimitExceeded
    AVIOInterruptCB callback();

private:
    std::chrono::steady_clock::time_point at_;
};

// ── input ─────────────────────────────────────────────────────────────────────────────────────────────────
// Demuxer over the range-pull channel. Seekable (AVSEEK_SIZE answers the declared input size), so files with the
// moov atom at the end are read in place; nothing is materialized. Nested opens (references, playlists) are
// refused and the demuxer set is whitelisted.
class Input {
public:
    Input(helpers::RangeClient& range, Deadline& deadline);
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    [[nodiscard]] AVFormatContext* fmt() const { return fmt_; }
    [[nodiscard]] std::string container() const;         // normalized (browser::normalizeContainer)
    [[nodiscard]] std::optional<double> durationSeconds() const;
    int bestVideoStream() const;                         // -1 when none (attached pictures excluded)
    int bestAudioStream() const;                         // -1 when none
    int attachedPictureStream() const;                   // -1 when none

    // After a failing libav call: rethrows the stored I/O error, a deadline overrun, or the given fallback.
    [[noreturn]] void raise(int err, const std::string& what) const;
    void rethrowStored() const;

private:
    static int readPacket(void* opaque, uint8_t* buf, int size);
    static int64_t seek(void* opaque, int64_t offset, int whence);

    helpers::RangeClient& range_;
    Deadline& deadline_;
    uint64_t pos_ = 0;
    std::exception_ptr error_;
    AVIOContext* avio_ = nullptr;
    AVFormatContext* fmt_ = nullptr;
};

double toSeconds(int64_t ts, AVRational tb);
int rotationDegrees(const AVStream* st);  // clockwise display rotation: 0, 90, 180, 270

// ── output ────────────────────────────────────────────────────────────────────────────────────────────────
// Write-only AVIOContext onto the OutputSink. It reports itself as non-seekable (so muxers stream), but keeps the
// last kHoldBack bytes unsent so a muxer's backwards size patch (box sizes) can still land; earlier bytes are final.
class SinkIo {
public:
    explicit SinkIo(helpers::OutputSink& sink);
    ~SinkIo();
    SinkIo(const SinkIo&) = delete;
    SinkIo& operator=(const SinkIo&) = delete;

    [[nodiscard]] AVIOContext* avio() const { return avio_; }
    void finish();                 // flushes libav buffers and emits the held-back tail
    void rethrowStored() const;

private:
#if FF_API_AVIO_WRITE_NONCONST
    static int writePacket(void* opaque, uint8_t* buf, int size);
#else
    static int writePacket(void* opaque, const uint8_t* buf, int size);
#endif
    static int64_t seek(void* opaque, int64_t offset, int whence);
    void emit(std::size_t count);

    static constexpr std::size_t kHoldBack = 4u << 20;
    helpers::OutputSink& sink_;
    AVIOContext* avio_ = nullptr;
    std::vector<uint8_t> tail_;    // bytes [emitted_, emitted_ + tail_.size())
    uint64_t emitted_ = 0;
    uint64_t pos_ = 0;
    std::exception_ptr error_;
};

}
