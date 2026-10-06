#include "Ffmpeg.hpp"
#include "Browser.hpp"

extern "C" {
#include <libavutil/display.h>
}

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>

namespace vh::media {

namespace ffmpeg_detail {

// Demuxers a browser-facing media preview has any business opening. Everything else is refused before probing
// deeper (smaller hostile-parser surface).
constexpr const char* kDemuxerWhitelist =
    "mov,mp4,m4a,3gp,3g2,mj2,matroska,webm,ogg,mp3,flac,wav,aac,avi,mpegts,flv,asf,mpeg,aiff,caf";

int refuseNestedOpen(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) { return AVERROR(EPERM); }

int interruptCallback(void* opaque) { return static_cast<const Deadline*>(opaque)->expired() ? 1 : 0; }

}

FramePtr makeFrame() {
    FramePtr f(av_frame_alloc());
    if (!f) throw std::bad_alloc();
    return f;
}

PacketPtr makePacket() {
    PacketPtr p(av_packet_alloc());
    if (!p) throw std::bad_alloc();
    return p;
}

std::string avError(const int err) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buf{};
    av_strerror(err, buf.data(), buf.size());
    return buf.data();
}

std::string option(const helpers::Args& args, const std::string& key, std::string fallback) {
    const auto it = args.options.find(key);
    return it == args.options.end() ? std::move(fallback) : it->second;
}

double numberOption(const helpers::Args& args, const std::string& key, const double fallback, const double min,
                    const double max) {
    const auto it = args.options.find(key);
    if (it == args.options.end()) return fallback;
    double value = 0;
    const auto& s = it->second;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || !std::isfinite(value) || value < min || value > max)
        throw std::invalid_argument("--" + key + " expects a number in [" + std::to_string(min) + ", " +
                                    std::to_string(max) + "]");
    return value;
}

// ── Deadline ──────────────────────────────────────────────────────────────────────────────────────────────

Deadline::Deadline(const std::chrono::seconds budget) : at_(std::chrono::steady_clock::now() + budget) {}

bool Deadline::expired() const { return std::chrono::steady_clock::now() >= at_; }

void Deadline::check() const {
    if (expired()) throw helpers::LimitExceeded("deadline exceeded");
}

AVIOInterruptCB Deadline::callback() { return AVIOInterruptCB{&ffmpeg_detail::interruptCallback, this}; }

// ── Input ─────────────────────────────────────────────────────────────────────────────────────────────────

Input::Input(helpers::RangeClient& range, Deadline& deadline) : range_(range), deadline_(deadline) {
    if (range_.size() == 0) throw helpers::InvalidInput("empty input");

    constexpr int kBufferSize = 128 * 1024;
    auto* buffer = static_cast<uint8_t*>(av_malloc(kBufferSize));
    if (!buffer) throw std::bad_alloc();
    avio_ = avio_alloc_context(buffer, kBufferSize, 0, this, &Input::readPacket, nullptr, &Input::seek);
    if (!avio_) {
        av_free(buffer);
        throw std::bad_alloc();
    }

    try {
        fmt_ = avformat_alloc_context();
        if (!fmt_) throw std::bad_alloc();
        fmt_->pb = avio_;
        fmt_->flags |= AVFMT_FLAG_CUSTOM_IO;
        fmt_->probesize = kProbeSize;
        fmt_->max_analyze_duration = kAnalyzeDurationUs;
        fmt_->max_streams = static_cast<int>(kMaxStreams);
        fmt_->interrupt_callback = deadline_.callback();
        fmt_->format_whitelist = av_strdup(ffmpeg_detail::kDemuxerWhitelist);
        fmt_->io_open = &ffmpeg_detail::refuseNestedOpen;

        if (const int rc = avformat_open_input(&fmt_, nullptr, nullptr, nullptr); rc < 0)
            raise(rc, "unrecognized or corrupt media");  // fmt_ was freed and nulled
        if (const int rc = avformat_find_stream_info(fmt_, nullptr); rc < 0)
            raise(rc, "could not read stream parameters");

        if (fmt_->nb_streams == 0) throw helpers::InvalidInput("no streams");
        if (fmt_->nb_streams > kMaxStreams) throw helpers::LimitExceeded("too many streams");
        for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
            AVStream* st = fmt_->streams[i];
            const AVCodecParameters* par = st->codecpar;
            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                const bool oversized = par->width < 0 || par->height < 0 || par->width > kMaxDimension ||
                                       par->height > kMaxDimension ||
                                       static_cast<int64_t>(par->width) * par->height > kMaxPixels;
                if (oversized && (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) st->discard = AVDISCARD_ALL;
                else if (oversized)
                    throw helpers::LimitExceeded("video dimensions " + std::to_string(par->width) + "x" +
                                                 std::to_string(par->height) + " exceed the limit");
            } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
                if (par->ch_layout.nb_channels > kMaxChannels || par->sample_rate > kMaxSampleRate ||
                    par->sample_rate < 0)
                    throw helpers::LimitExceeded("audio parameters exceed the limit");
            }
        }
    } catch (...) {
        if (fmt_) avformat_close_input(&fmt_);
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
        throw;
    }
}

Input::~Input() {
    if (fmt_) avformat_close_input(&fmt_);
    if (avio_) {
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
    }
}

void Input::rethrowStored() const {
    if (error_) std::rethrow_exception(error_);
}

void Input::raise(const int err, const std::string& what) const {
    rethrowStored();
    if (err == AVERROR_EXIT || deadline_.expired()) throw helpers::LimitExceeded("deadline exceeded");
    if (err == AVERROR(ENOMEM)) throw helpers::LimitExceeded(what + ": out of memory");
    throw helpers::InvalidInput(what + ": " + avError(err));
}

int Input::readPacket(void* opaque, uint8_t* buf, const int size) {
    auto* self = static_cast<Input*>(opaque);
    try {
        if (self->deadline_.expired()) return AVERROR_EXIT;
        if (self->pos_ >= self->range_.size()) return AVERROR_EOF;
        const auto n = self->range_.read(self->pos_, std::span(buf, static_cast<std::size_t>(size)));
        if (n == 0) return AVERROR_EOF;
        self->pos_ += n;
        return static_cast<int>(n);
    } catch (...) {
        self->error_ = std::current_exception();
        return AVERROR(EIO);
    }
}

int64_t Input::seek(void* opaque, const int64_t offset, int whence) {
    auto* self = static_cast<Input*>(opaque);
    const auto size = static_cast<int64_t>(self->range_.size());
    if (whence & AVSEEK_SIZE) return size;
    whence &= ~AVSEEK_FORCE;
    int64_t base = 0;
    if (whence == SEEK_CUR) base = static_cast<int64_t>(self->pos_);
    else if (whence == SEEK_END) base = size;
    else if (whence != SEEK_SET) return AVERROR(EINVAL);
    const int64_t target = base + offset;
    if (target < 0) return AVERROR(EINVAL);
    self->pos_ = static_cast<uint64_t>(target);
    return target;
}

std::string Input::container() const {
    const std::string_view name = fmt_->iformat && fmt_->iformat->name ? fmt_->iformat->name : "";
    const AVDictionaryEntry* brand = av_dict_get(fmt_->metadata, "major_brand", nullptr, 0);
    std::string doc;
    if (name.find("matroska") != std::string_view::npos) {
        std::array<uint8_t, 64> head{};
        const auto n = range_.read(0, head);
        doc = browser::matroskaDocType(std::span(head.data(), n));
    }
    return browser::normalizeContainer(name, brand ? brand->value : "", doc);
}

std::optional<double> Input::durationSeconds() const {
    if (fmt_->duration == AV_NOPTS_VALUE || fmt_->duration <= 0) return std::nullopt;
    return static_cast<double>(fmt_->duration) / AV_TIME_BASE;
}

int Input::bestVideoStream() const {
    int best = -1;
    int64_t bestPixels = -1;
    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        const AVStream* st = fmt_->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO || (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) continue;
        int64_t pixels = static_cast<int64_t>(st->codecpar->width) * st->codecpar->height;
        if (st->disposition & AV_DISPOSITION_DEFAULT) pixels += kMaxPixels + 1;  // a default stream wins
        if (pixels > bestPixels) {
            best = static_cast<int>(i);
            bestPixels = pixels;
        }
    }
    return best;
}

int Input::bestAudioStream() const {
    const int related = bestVideoStream();
    const int rc = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, related, nullptr, 0);
    return rc >= 0 ? rc : -1;
}

int Input::attachedPictureStream() const {
    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        const AVStream* st = fmt_->streams[i];
        if ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) && st->discard != AVDISCARD_ALL &&
            st->attached_pic.size > 0)
            return static_cast<int>(i);
    }
    return -1;
}

double toSeconds(const int64_t ts, const AVRational tb) { return static_cast<double>(ts) * av_q2d(tb); }

int rotationDegrees(const AVStream* st) {
    const AVPacketSideData* sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                                         st->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * sizeof(int32_t)) return 0;
    const double theta = -av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    if (!std::isfinite(theta)) return 0;
    const long quarter = std::lround(theta / 90.0);
    return static_cast<int>(((quarter % 4) + 4) % 4) * 90;
}

// ── SinkIo ────────────────────────────────────────────────────────────────────────────────────────────────

SinkIo::SinkIo(helpers::OutputSink& sink) : sink_(sink) {
    constexpr int kBufferSize = 64 * 1024;
    auto* buffer = static_cast<uint8_t*>(av_malloc(kBufferSize));
    if (!buffer) throw std::bad_alloc();
    avio_ = avio_alloc_context(buffer, kBufferSize, 1, this, nullptr, &SinkIo::writePacket, &SinkIo::seek);
    if (!avio_) {
        av_free(buffer);
        throw std::bad_alloc();
    }
    avio_->seekable = 0;  // muxers must stream; seek() only serves backwards box-size patches in the hold-back
}

SinkIo::~SinkIo() {
    if (avio_) {
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
    }
}

void SinkIo::rethrowStored() const {
    if (error_) std::rethrow_exception(error_);
}

void SinkIo::emit(std::size_t count) {
    count = std::min<std::size_t>({count, tail_.size(), static_cast<std::size_t>(pos_ - emitted_)});
    if (count == 0) return;
    sink_.write(std::span<const uint8_t>(tail_.data(), count));
    tail_.erase(tail_.begin(), tail_.begin() + static_cast<std::ptrdiff_t>(count));
    emitted_ += count;
}

#if FF_API_AVIO_WRITE_NONCONST
int SinkIo::writePacket(void* opaque, uint8_t* buf, const int size) {
#else
int SinkIo::writePacket(void* opaque, const uint8_t* buf, const int size) {
#endif
    auto* self = static_cast<SinkIo*>(opaque);
    try {
        if (self->pos_ < self->emitted_) throw std::runtime_error("muxer rewrote bytes already sent");
        const auto offset = static_cast<std::size_t>(self->pos_ - self->emitted_);
        const auto end = offset + static_cast<std::size_t>(size);
        if (end > self->tail_.size()) self->tail_.resize(end);
        std::memcpy(self->tail_.data() + offset, buf, static_cast<std::size_t>(size));
        self->pos_ += static_cast<uint64_t>(size);
        if (self->tail_.size() >= 2 * kHoldBack) self->emit(self->tail_.size() - kHoldBack);
        return size;
    } catch (...) {
        self->error_ = std::current_exception();
        return AVERROR(EIO);
    }
}

int64_t SinkIo::seek(void* opaque, const int64_t offset, int whence) {
    auto* self = static_cast<SinkIo*>(opaque);
    if (whence & AVSEEK_SIZE) return AVERROR(ENOSYS);
    whence &= ~AVSEEK_FORCE;
    int64_t base = 0;
    if (whence == SEEK_CUR) base = static_cast<int64_t>(self->pos_);
    else if (whence == SEEK_END) base = static_cast<int64_t>(self->emitted_ + self->tail_.size());
    else if (whence != SEEK_SET) return AVERROR(EINVAL);
    const int64_t target = base + offset;
    if (target < static_cast<int64_t>(self->emitted_)) return AVERROR(EPIPE);
    self->pos_ = static_cast<uint64_t>(target);
    return target;
}

void SinkIo::finish() {
    avio_flush(avio_);
    rethrowStored();
    if (avio_->error < 0) throw std::runtime_error("output: " + avError(avio_->error));
    pos_ = emitted_ + tail_.size();
    emit(tail_.size());
}

}
