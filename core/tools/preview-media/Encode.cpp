// transcode (fragmented MP4 on stdout) and hls (framed init.mp4 + seg-NNNNN.m4s + index.m3u8).
//
// Pipeline: range-pull demux → software decode → swscale (box fit, square pixels) → H.264 encoder chosen by
// hw::candidates (VAAPI → QSV → NVENC → libx264, falling back on any open/test-encode failure) and
// swresample → AAC-LC stereo 48 kHz. Display rotation is carried as container metadata, not applied to pixels.

#include "Commands.hpp"
#include "Ffmpeg.hpp"
#include "Hardware.hpp"

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>

namespace vh::media {

namespace encode_detail {

struct Profile {
    std::string_view name;
    int longSide;
    int shortSide;
    int64_t maxRate;
    int64_t bufSize;
};

constexpr Profile kProfiles[] = {
    {"h264-480", 854, 480, 1'500'000, 3'000'000},
    {"h264-720", 1280, 720, 4'000'000, 8'000'000},
    {"h264-1080", 1920, 1080, 8'000'000, 16'000'000},
};

constexpr AVRational kVideoTb{1, 90000};
constexpr int kAudioRate = 48000;
constexpr int64_t kAudioBitRate = 128000;
constexpr double kMaxFps = 60.0;
constexpr int kMaxDecodeErrors = 500;

const Profile& findProfile(const std::string_view name) {
    for (const auto& p : kProfiles)
        if (p.name == name) return p;
    throw std::invalid_argument("--profile expects h264-480|h264-720|h264-1080");
}

struct FormatContextDeleter { void operator()(AVFormatContext* c) const { avformat_free_context(c); } };
using OutputContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

struct AudioFifoDeleter { void operator()(AVAudioFifo* f) const { av_audio_fifo_free(f); } };
using AudioFifoPtr = std::unique_ptr<AVAudioFifo, AudioFifoDeleter>;

int even(const double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); }

FramePtr allocVideoFrame(const int width, const int height, const AVPixelFormat format) {
    auto f = makeFrame();
    f->width = width;
    f->height = height;
    f->format = format;
    if (av_frame_get_buffer(f.get(), 0) < 0) throw std::bad_alloc();
    return f;
}

struct VideoParams {
    int width = 0;
    int height = 0;
    AVRational frameRate{30, 1};
    int threads = 2;
    const Profile* profile = nullptr;
};

// Opens the H.264 encoder for one accelerator; returns nullptr and fills `error` on failure.
CodecContextPtr openVideoEncoder(const hw::Accel accel, const VideoParams& p, std::string& error,
                                 BufferRefPtr& framesOut) {
    const AVCodec* codec = avcodec_find_encoder_by_name(hw::encoderName(accel));
    if (!codec) {
        error = std::string(hw::encoderName(accel)) + " is not in this FFmpeg build";
        return nullptr;
    }
    if (accel != hw::Accel::Software && !hw::device(accel)) {
        error = hw::unavailableReason(accel);
        return nullptr;
    }

    CodecContextPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx) throw std::bad_alloc();
    ctx->width = p.width;
    ctx->height = p.height;
    ctx->time_base = kVideoTb;
    ctx->framerate = p.frameRate;
    ctx->sample_aspect_ratio = AVRational{1, 1};
    ctx->gop_size = std::max(1, static_cast<int>(std::lround(av_q2d(p.frameRate) * 2)));
    ctx->color_range = AVCOL_RANGE_MPEG;
    ctx->colorspace = AVCOL_SPC_BT709;
    ctx->color_primaries = AVCOL_PRI_BT709;
    ctx->color_trc = AVCOL_TRC_BT709;
    ctx->rc_max_rate = p.profile->maxRate;
    ctx->rc_buffer_size = static_cast<int>(p.profile->bufSize);
    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;  // mp4 and the hls fmp4 muxer both want extradata
    ctx->thread_count = p.threads;

    AVDictionary* opts = nullptr;
    switch (accel) {
        case hw::Accel::Software:
            ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            av_dict_set(&opts, "preset", "veryfast", 0);
            av_dict_set(&opts, "crf", "23", 0);
            av_dict_set(&opts, "profile", "high", 0);
            av_dict_set(&opts, "forced-idr", "1", 0);
            break;
        case hw::Accel::Vaapi: {
            ctx->pix_fmt = AV_PIX_FMT_VAAPI;
            ctx->bit_rate = p.profile->maxRate * 3 / 4;
            BufferRefPtr frames(av_hwframe_ctx_alloc(hw::device(accel)));
            if (!frames) throw std::bad_alloc();
            auto* fc = reinterpret_cast<AVHWFramesContext*>(frames->data);
            fc->format = AV_PIX_FMT_VAAPI;
            fc->sw_format = AV_PIX_FMT_NV12;
            fc->width = p.width;
            fc->height = p.height;
            fc->initial_pool_size = 20;
            if (const int rc = av_hwframe_ctx_init(frames.get()); rc < 0) {
                error = "VAAPI frame pool: " + avError(rc);
                return nullptr;
            }
            ctx->hw_frames_ctx = av_buffer_ref(frames.get());
            framesOut = std::move(frames);
            av_dict_set(&opts, "profile", "high", 0);
            break;
        }
        case hw::Accel::Qsv:
            ctx->pix_fmt = AV_PIX_FMT_NV12;
            ctx->bit_rate = p.profile->maxRate * 3 / 4;
            av_dict_set(&opts, "preset", "veryfast", 0);
            av_dict_set(&opts, "forced_idr", "1", 0);
            av_dict_set(&opts, "profile", "high", 0);
            break;
        case hw::Accel::Nvenc:
            ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            ctx->bit_rate = p.profile->maxRate * 3 / 4;
            ctx->hw_device_ctx = av_buffer_ref(hw::device(accel));
            av_dict_set(&opts, "preset", "p4", 0);
            av_dict_set(&opts, "rc", "vbr", 0);
            av_dict_set(&opts, "cq", "23", 0);
            av_dict_set(&opts, "profile", "high", 0);
            av_dict_set(&opts, "forced-idr", "1", 0);
            break;
    }
    const int rc = avcodec_open2(ctx.get(), codec, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        error = std::string(hw::encoderName(accel)) + ": " + avError(rc);
        framesOut.reset();
        return nullptr;
    }
    return ctx;
}

AVPixelFormat softwareFormat(const hw::Accel accel) {
    return accel == hw::Accel::Vaapi || accel == hw::Accel::Qsv ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
}

FramePtr blackFrame(const int width, const int height, const AVPixelFormat format) {
    auto f = allocVideoFrame(width, height, format);
    for (int y = 0; y < height; ++y) std::memset(f->data[0] + y * f->linesize[0], 16, static_cast<std::size_t>(width));
    if (format == AV_PIX_FMT_NV12) {
        for (int y = 0; y < height / 2; ++y) std::memset(f->data[1] + y * f->linesize[1], 128, static_cast<std::size_t>(width));
    } else {
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < height / 2; ++y)
                std::memset(f->data[p] + y * f->linesize[p], 128, static_cast<std::size_t>(width / 2));
    }
    f->pts = 0;
    return f;
}

// Hardware encoders can open fine and then fail on the first frame (driver/profile mismatch). Encode one black
// frame on a throwaway encoder before committing the real stream to an accelerator.
bool testEncode(const hw::Accel accel, const VideoParams& p, std::string& error) {
    BufferRefPtr frames;
    auto ctx = openVideoEncoder(accel, p, error, frames);
    if (!ctx) return false;
    FramePtr frame = blackFrame(p.width, p.height, softwareFormat(accel));
    if (accel == hw::Accel::Vaapi) {
        auto hwFrame = makeFrame();
        if (const int rc = av_hwframe_get_buffer(frames.get(), hwFrame.get(), 0); rc < 0) {
            error = "VAAPI surface: " + avError(rc);
            return false;
        }
        if (const int rc = av_hwframe_transfer_data(hwFrame.get(), frame.get(), 0); rc < 0) {
            error = "VAAPI upload: " + avError(rc);
            return false;
        }
        hwFrame->pts = 0;
        frame = std::move(hwFrame);
    }
    if (const int rc = avcodec_send_frame(ctx.get(), frame.get()); rc < 0) {
        error = "test encode: " + avError(rc);
        return false;
    }
    avcodec_send_frame(ctx.get(), nullptr);
    auto pkt = makePacket();
    bool gotPacket = false;
    for (;;) {
        const int rc = avcodec_receive_packet(ctx.get(), pkt.get());
        if (rc == AVERROR_EOF) break;
        if (rc < 0) {
            error = "test encode: " + avError(rc);
            return false;
        }
        gotPacket = true;
        av_packet_unref(pkt.get());
    }
    if (!gotPacket) error = "test encode produced no packet";
    return gotPacket;
}

class Transcoder {
public:
    Transcoder(Input& in, const Profile& profile, const std::string& hwaccel, const int threads, Deadline& deadline,
               const double keyInterval)
        : in_(in), deadline_(deadline), keyInterval_(keyInterval) {
        AVFormatContext* fmt = in_.fmt();
        vIndex_ = in_.bestVideoStream();
        aIndex_ = in_.bestAudioStream();
        if (vIndex_ < 0 && aIndex_ < 0) throw helpers::Unsupported("no audio or video stream");
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
            fmt->streams[i]->discard = static_cast<int>(i) == vIndex_ || static_cast<int>(i) == aIndex_
                                           ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
        startUs_ = fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time : 0;
        if (vIndex_ >= 0) setupVideo(profile, hwaccel, threads);
        if (aIndex_ >= 0) {
            try {
                setupAudio();
            } catch (const helpers::Unsupported& e) {
                if (!venc_) throw;
                audioSkipped_ = e.what();  // keep the picture; an exotic audio codec is not worth failing over
                adec_.reset();
                aenc_.reset();
                in_.fmt()->streams[aIndex_]->discard = AVDISCARD_ALL;
            }
        }
    }

    ~Transcoder() = default;
    Transcoder(const Transcoder&) = delete;
    Transcoder& operator=(const Transcoder&) = delete;

    [[nodiscard]] bool hasVideo() const { return venc_ != nullptr; }

    void addStreams(AVFormatContext* out) {
        if (venc_) {
            vout_ = avformat_new_stream(out, nullptr);
            if (!vout_ || avcodec_parameters_from_context(vout_->codecpar, venc_.get()) < 0) throw std::bad_alloc();
            vout_->time_base = venc_->time_base;
            vout_->avg_frame_rate = venc_->framerate;
            const AVCodecParameters* src = in_.fmt()->streams[vIndex_]->codecpar;
            if (const AVPacketSideData* sd = av_packet_side_data_get(src->coded_side_data, src->nb_coded_side_data,
                                                                     AV_PKT_DATA_DISPLAYMATRIX)) {
                auto* copy = static_cast<uint8_t*>(av_memdup(sd->data, sd->size));
                if (copy && !av_packet_side_data_add(&vout_->codecpar->coded_side_data,
                                                     &vout_->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX,
                                                     copy, sd->size, 0))
                    av_free(copy);
            }
        }
        if (aenc_) {
            aout_ = avformat_new_stream(out, nullptr);
            if (!aout_ || avcodec_parameters_from_context(aout_->codecpar, aenc_.get()) < 0) throw std::bad_alloc();
            aout_->time_base = aenc_->time_base;
            if (const AVDictionaryEntry* lang = av_dict_get(in_.fmt()->streams[aIndex_]->metadata, "language", nullptr, 0))
                av_dict_set(&aout_->metadata, "language", lang->value, 0);
        }
    }

    void run(AVFormatContext* out, const std::function<void(int, const char*)>& muxFailed) {
        out_ = out;
        muxFailed_ = &muxFailed;
        auto pkt = makePacket();
        for (;;) {
            deadline_.check();
            const int rc = av_read_frame(in_.fmt(), pkt.get());
            if (rc == AVERROR_EXIT) in_.raise(rc, "read");
            if (rc < 0) {
                in_.rethrowStored();
                break;  // EOF, or a corrupt tail: keep what decoded (like ffmpeg)
            }
            if (pkt->stream_index == vIndex_ && vdec_) decode(vdec_.get(), pkt.get(), true);
            else if (pkt->stream_index == aIndex_ && adec_) decode(adec_.get(), pkt.get(), false);
            av_packet_unref(pkt.get());
        }
        if (vdec_) decode(vdec_.get(), nullptr, true);
        if (adec_) decode(adec_.get(), nullptr, false);
        if (venc_) encodeVideo(nullptr);
        if (aenc_) finishAudio();

        if (venc_ && videoFrames_ == 0) throw helpers::InvalidInput("video stream produced no decodable frames");
        if (aenc_ && !venc_ && audioSamples_ == 0) throw helpers::InvalidInput("audio stream produced no decodable frames");
    }

    [[nodiscard]] nlohmann::json describe() const {
        nlohmann::json j;
        j["encoder"] = venc_ ? hw::toString(accel_) : "software";
        j["video_encoder"] = venc_ ? nlohmann::json(hw::encoderName(accel_)) : nlohmann::json();
        j["hwaccel_fallbacks"] = fallbacks_;
        if (venc_) {
            j["video"] = {{"codec", "h264"}, {"width", venc_->width}, {"height", venc_->height},
                          {"frames", videoFrames_}, {"fps", av_q2d(venc_->framerate)}, {"rotation", rotation_}};
        } else {
            j["video"] = nullptr;
        }
        if (aenc_) {
            j["audio"] = {{"codec", "aac"}, {"sample_rate", kAudioRate}, {"channels", 2}, {"bit_rate", kAudioBitRate},
                          {"samples", audioSamples_}};
        } else {
            j["audio"] = nullptr;
        }
        if (!audioSkipped_.empty()) j["audio_skipped"] = audioSkipped_;
        j["decode_errors"] = decodeErrors_;
        return j;
    }

private:
    static CodecContextPtr openDecoder(const AVStream* st, const int threads) {
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) throw helpers::Unsupported(std::string("no decoder for ") + avcodec_get_name(st->codecpar->codec_id));
        CodecContextPtr ctx(avcodec_alloc_context3(codec));
        if (!ctx) throw std::bad_alloc();
        if (avcodec_parameters_to_context(ctx.get(), st->codecpar) < 0) throw helpers::InvalidInput("bad codec parameters");
        ctx->thread_count = threads;
        ctx->max_pixels = kMaxPixels;
        ctx->pkt_timebase = st->time_base;
        if (const int rc = avcodec_open2(ctx.get(), codec, nullptr); rc < 0)
            throw helpers::InvalidInput("could not open decoder: " + avError(rc));
        return ctx;
    }

    void setupVideo(const Profile& profile, const std::string& hwaccel, const int threads) {
        AVStream* st = in_.fmt()->streams[vIndex_];
        vdec_ = openDecoder(st, threads);
        rotation_ = rotationDegrees(st);

        double displayW = st->codecpar->width;
        const double displayH = st->codecpar->height;
        const AVRational sar = av_guess_sample_aspect_ratio(in_.fmt(), st, nullptr);
        if (sar.num > 0 && sar.den > 0) displayW *= av_q2d(sar);
        if (displayW <= 0 || displayH <= 0) throw helpers::InvalidInput("video stream has no dimensions");
        const double longSide = std::max(displayW, displayH), shortSide = std::min(displayW, displayH);
        const double factor = std::min({1.0, profile.longSide / longSide, profile.shortSide / shortSide});

        VideoParams p;
        p.width = even(displayW * factor);
        p.height = even(displayH * factor);
        p.threads = threads;
        p.profile = &profile;
        AVRational fr = av_guess_frame_rate(in_.fmt(), st, nullptr);
        if (fr.num <= 0 || fr.den <= 0 || av_q2d(fr) > 1000) fr = AVRational{30, 1};
        if (av_q2d(fr) > kMaxFps) fr = AVRational{60, 1};
        p.frameRate = fr;
        minFrameGap_ = av_q2d(fr) >= kMaxFps - 0.5 ? av_rescale_q(1, AVRational{1, 61}, kVideoTb) : 0;

        for (const hw::Accel accel : hw::candidates(hwaccel)) {
            std::string error;
            if (accel != hw::Accel::Software && !testEncode(accel, p, error)) {
                fallbacks_.push_back({{"accel", hw::toString(accel)}, {"reason", error}});
                continue;
            }
            BufferRefPtr frames;
            venc_ = openVideoEncoder(accel, p, error, frames);
            if (venc_) {
                accel_ = accel;
                hwFrames_ = std::move(frames);
                swFormat_ = softwareFormat(accel);
                return;
            }
            fallbacks_.push_back({{"accel", hw::toString(accel)}, {"reason", error}});
        }
        throw helpers::Unsupported("no usable H.264 encoder (libx264 missing from this FFmpeg build and no hardware "
                                   "encoder available)");
    }

    void setupAudio() {
        const AVStream* st = in_.fmt()->streams[aIndex_];
        adec_ = openDecoder(st, 1);
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) throw helpers::Unsupported("aac encoder not in this FFmpeg build");
        aenc_.reset(avcodec_alloc_context3(codec));
        if (!aenc_) throw std::bad_alloc();
        aenc_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        aenc_->sample_rate = kAudioRate;
        av_channel_layout_default(&aenc_->ch_layout, 2);
        aenc_->bit_rate = kAudioBitRate;
        aenc_->time_base = AVRational{1, kAudioRate};
        aenc_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (const int rc = avcodec_open2(aenc_.get(), codec, nullptr); rc < 0)
            throw std::runtime_error("aac encoder: " + avError(rc));
        fifo_.reset(av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, std::max(aenc_->frame_size, 1024) * 4));
        if (!fifo_) throw std::bad_alloc();
    }

    void decode(AVCodecContext* dec, const AVPacket* pkt, const bool video) {
        const int sent = avcodec_send_packet(dec, pkt);
        if (sent < 0 && sent != AVERROR_EOF) {
            if (sent == AVERROR(ENOMEM)) throw helpers::LimitExceeded("decoder out of memory");
            if (++decodeErrors_ > kMaxDecodeErrors) throw helpers::InvalidInput("too many decode errors");
            return;
        }
        auto frame = makeFrame();
        for (;;) {
            const int rc = avcodec_receive_frame(dec, frame.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return;
            if (rc < 0) {
                if (++decodeErrors_ > kMaxDecodeErrors) throw helpers::InvalidInput("too many decode errors");
                return;
            }
            if (video) pushVideo(frame.get());
            else pushAudio(frame.get());
            av_frame_unref(frame.get());
        }
    }

    void pushVideo(const AVFrame* decoded) {
        if (decoded->width <= 0 || decoded->height <= 0 || decoded->width > kMaxDimension ||
            decoded->height > kMaxDimension)
            throw helpers::LimitExceeded("decoded frame dimensions exceed the limit");
        const AVStream* st = in_.fmt()->streams[vIndex_];
        int64_t ts = decoded->best_effort_timestamp;
        int64_t pts = ts == AV_NOPTS_VALUE
                          ? (lastVideoPts_ == std::numeric_limits<int64_t>::min() ? 0 : lastVideoPts_ + 1)
                          : av_rescale_q(ts, st->time_base, kVideoTb) - av_rescale_q(startUs_, AV_TIME_BASE_Q, kVideoTb);
        if (pts < 0) return;  // pre-roll hidden by an edit list
        if (lastVideoPts_ != std::numeric_limits<int64_t>::min() && pts < lastVideoPts_ + std::max<int64_t>(1, minFrameGap_))
            return;  // non-monotonic, or above the frame-rate cap
        lastVideoPts_ = pts;

        SwsContext* reused = sws_getCachedContext(sws_.release(), decoded->width, decoded->height,
                                                  static_cast<AVPixelFormat>(decoded->format), venc_->width,
                                                  venc_->height, swFormat_, SWS_BICUBIC, nullptr, nullptr, nullptr);
        sws_.reset(reused);
        if (!sws_) throw helpers::Unsupported("cannot convert the video pixel format");
        const int srcSpace = decoded->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
        sws_setColorspaceDetails(sws_.get(), sws_getCoefficients(srcSpace), decoded->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                                 sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);

        FramePtr frame = allocVideoFrame(venc_->width, venc_->height, swFormat_);
        sws_scale(sws_.get(), decoded->data, decoded->linesize, 0, decoded->height, frame->data, frame->linesize);
        frame->pts = pts;
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        if (keyInterval_ > 0) {
            const double t = static_cast<double>(pts) * av_q2d(kVideoTb);
            if (t + 1e-6 >= nextKey_) {
                frame->pict_type = AV_PICTURE_TYPE_I;
                while (nextKey_ <= t + 1e-6) nextKey_ += keyInterval_;
            }
        }
        if (accel_ == hw::Accel::Vaapi) {
            auto hwFrame = makeFrame();
            if (const int rc = av_hwframe_get_buffer(hwFrames_.get(), hwFrame.get(), 0); rc < 0)
                throw std::runtime_error("VAAPI surface: " + avError(rc));
            if (const int rc = av_hwframe_transfer_data(hwFrame.get(), frame.get(), 0); rc < 0)
                throw std::runtime_error("VAAPI upload: " + avError(rc));
            hwFrame->pts = frame->pts;
            hwFrame->pict_type = frame->pict_type;
            frame = std::move(hwFrame);
        }
        ++videoFrames_;
        encodeVideo(frame.get());
    }

    void encodeVideo(const AVFrame* frame) { encode(venc_.get(), frame, vout_); }

    void pushAudio(const AVFrame* decoded) {
        if (!swr_) {
            AVChannelLayout inLayout{};
            if (decoded->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || decoded->ch_layout.nb_channels <= 0)
                av_channel_layout_default(&inLayout, std::max(1, decoded->ch_layout.nb_channels));
            else if (av_channel_layout_copy(&inLayout, &decoded->ch_layout) < 0) throw std::bad_alloc();
            if (inLayout.nb_channels > kMaxChannels) throw helpers::LimitExceeded("too many audio channels");
            SwrContext* raw = nullptr;
            const int rc = swr_alloc_set_opts2(&raw, &aenc_->ch_layout, AV_SAMPLE_FMT_FLTP, kAudioRate, &inLayout,
                                               static_cast<AVSampleFormat>(decoded->format), decoded->sample_rate, 0, nullptr);
            av_channel_layout_uninit(&inLayout);
            swr_.reset(raw);
            if (rc < 0 || swr_init(swr_.get()) < 0) throw helpers::Unsupported("cannot resample this audio");
            inRate_ = decoded->sample_rate;
            inChannels_ = decoded->ch_layout.nb_channels;

            const AVStream* st = in_.fmt()->streams[aIndex_];
            const int64_t ts = decoded->best_effort_timestamp;
            const AVRational outTb{1, kAudioRate};
            nextAudioPts_ = ts == AV_NOPTS_VALUE ? 0
                                                 : std::max<int64_t>(0, av_rescale_q(ts, st->time_base, outTb) -
                                                                            av_rescale_q(startUs_, AV_TIME_BASE_Q, outTb));
        }
        if (decoded->sample_rate != inRate_ || decoded->ch_layout.nb_channels != inChannels_) {
            ++decodeErrors_;  // mid-stream parameter change: drop rather than mis-resample
            return;
        }
        convert(const_cast<const uint8_t**>(decoded->extended_data), decoded->nb_samples);
        drainFifo(false);
    }

    void convert(const uint8_t** data, const int samples) {
        const int capacity = swr_get_out_samples(swr_.get(), samples);
        if (capacity <= 0) return;
        auto tmp = makeFrame();
        tmp->nb_samples = capacity;
        tmp->format = AV_SAMPLE_FMT_FLTP;
        tmp->sample_rate = kAudioRate;
        if (av_channel_layout_copy(&tmp->ch_layout, &aenc_->ch_layout) < 0 || av_frame_get_buffer(tmp.get(), 0) < 0)
            throw std::bad_alloc();
        const int got = swr_convert(swr_.get(), tmp->data, capacity, data, samples);
        if (got < 0) throw std::runtime_error("resample failed");
        if (got > 0 && av_audio_fifo_write(fifo_.get(), reinterpret_cast<void**>(tmp->data), got) < got)
            throw std::bad_alloc();
    }

    void drainFifo(const bool final) {
        const int frameSize = aenc_->frame_size > 0 ? aenc_->frame_size : 1024;
        while (av_audio_fifo_size(fifo_.get()) >= frameSize || (final && av_audio_fifo_size(fifo_.get()) > 0)) {
            const int n = std::min(frameSize, av_audio_fifo_size(fifo_.get()));
            auto frame = makeFrame();
            frame->nb_samples = n;
            frame->format = AV_SAMPLE_FMT_FLTP;
            frame->sample_rate = kAudioRate;
            if (av_channel_layout_copy(&frame->ch_layout, &aenc_->ch_layout) < 0 || av_frame_get_buffer(frame.get(), 0) < 0)
                throw std::bad_alloc();
            if (av_audio_fifo_read(fifo_.get(), reinterpret_cast<void**>(frame->data), n) < n)
                throw std::runtime_error("audio fifo underrun");
            frame->pts = nextAudioPts_;
            nextAudioPts_ += n;
            audioSamples_ += n;
            encode(aenc_.get(), frame.get(), aout_);
        }
    }

    void finishAudio() {
        if (swr_) {
            convert(nullptr, 0);
            drainFifo(true);
        }
        encode(aenc_.get(), nullptr, aout_);
    }

    void encode(AVCodecContext* enc, const AVFrame* frame, AVStream* stream) {
        if (const int rc = avcodec_send_frame(enc, frame); rc < 0 && rc != AVERROR_EOF)
            throw std::runtime_error(std::string("encoder ") + enc->codec->name + ": " + avError(rc));
        auto pkt = makePacket();
        for (;;) {
            const int rc = avcodec_receive_packet(enc, pkt.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return;
            if (rc < 0) throw std::runtime_error(std::string("encoder ") + enc->codec->name + ": " + avError(rc));
            pkt->stream_index = stream->index;
            av_packet_rescale_ts(pkt.get(), enc->time_base, stream->time_base);
            if (const int wrc = av_interleaved_write_frame(out_, pkt.get()); wrc < 0)
                (*muxFailed_)(wrc, "write");
        }
    }

    Input& in_;
    Deadline& deadline_;
    double keyInterval_;
    double nextKey_ = 0;
    int vIndex_ = -1;
    int aIndex_ = -1;
    int64_t startUs_ = 0;
    int rotation_ = 0;
    int decodeErrors_ = 0;
    std::string audioSkipped_;

    CodecContextPtr vdec_;
    CodecContextPtr venc_;
    hw::Accel accel_ = hw::Accel::Software;
    nlohmann::json fallbacks_ = nlohmann::json::array();
    BufferRefPtr hwFrames_;
    AVPixelFormat swFormat_ = AV_PIX_FMT_YUV420P;
    SwsPtr sws_;
    int64_t lastVideoPts_ = std::numeric_limits<int64_t>::min();
    int64_t minFrameGap_ = 0;
    int64_t videoFrames_ = 0;

    CodecContextPtr adec_;
    CodecContextPtr aenc_;
    SwrPtr swr_;
    AudioFifoPtr fifo_;
    int inRate_ = 0;
    int inChannels_ = 0;
    int64_t nextAudioPts_ = 0;
    int64_t audioSamples_ = 0;

    AVFormatContext* out_ = nullptr;
    AVStream* vout_ = nullptr;
    AVStream* aout_ = nullptr;
    const std::function<void(int, const char*)>* muxFailed_ = nullptr;
};

struct Common {
    const Profile* profile;
    std::string hwaccel;
    int threads;
    std::chrono::seconds deadline;
    double maxDuration;
};

Common parseCommon(const helpers::Args& args) {
    Common c;
    c.profile = &findProfile(option(args, "profile", "h264-720"));
    c.hwaccel = option(args, "hwaccel", "software");
    if (!hw::validHwaccel(c.hwaccel)) throw std::invalid_argument("--hwaccel expects auto|software|vaapi|qsv|nvenc");
    c.threads = static_cast<int>(numberOption(args, "threads", 2, 1, 4));
    c.deadline = std::chrono::seconds(static_cast<int64_t>(numberOption(args, "deadline-seconds", 3600, 1, 86400)));
    c.maxDuration = numberOption(args, "max-duration-seconds", 6 * 3600, 1, 7 * 86400);
    return c;
}

void checkDuration(const Input& in, const double maxDuration) {
    if (const auto d = in.durationSeconds(); d && *d > maxDuration)
        throw helpers::LimitExceeded("duration exceeds " + std::to_string(static_cast<int64_t>(maxDuration)) + " s");
}

// ── HLS collector: the hls muxer opens every output through io_open; each becomes an in-memory buffer ────
struct HlsCollector {
    explicit HlsCollector(helpers::OutputSink& sink) : framed(sink) {}
    helpers::FramedOutput framed;
    std::map<AVIOContext*, std::string> open;
    std::vector<uint8_t> playlist;
    std::vector<uint8_t> init;
    nlohmann::json segments = nlohmann::json::array();
    std::exception_ptr error;
};

HlsCollector* gHls = nullptr;  // the hls muxer and its fmp4 child are single-threaded; one collector per process

constexpr std::string_view kPlaylistName = "index.m3u8";
constexpr std::string_view kInitName = "init.mp4";

bool safeName(const std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == '.') return false;
    return std::ranges::all_of(name, [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    });
}

int hlsOpen(AVFormatContext*, AVIOContext** pb, const char* url, const int flags, AVDictionary**) {
    if (!gHls || !(flags & AVIO_FLAG_WRITE) || !url) return AVERROR(EPERM);
    std::string_view name(url);
    if (const auto slash = name.rfind('/'); slash != std::string_view::npos) name.remove_prefix(slash + 1);
    if (!safeName(name)) return AVERROR(EPERM);
    if (const int rc = avio_open_dyn_buf(pb); rc < 0) return rc;
    gHls->open[*pb] = std::string(name);
    return 0;
}

int hlsClose(AVFormatContext*, AVIOContext* pb) {
    if (!pb) return 0;
    uint8_t* data = nullptr;
    const int size = avio_close_dyn_buf(pb, &data);
    std::string name;
    if (gHls) {
        if (const auto it = gHls->open.find(pb); it != gHls->open.end()) {
            name = it->second;
            gHls->open.erase(it);
        }
    }
    int rc = 0;
    try {
        const std::span<const uint8_t> bytes(data, size > 0 ? static_cast<std::size_t>(size) : 0);
        if (!gHls || name.empty()) {
            rc = AVERROR(EINVAL);
        } else if (name == kPlaylistName) {
            gHls->playlist.assign(bytes.begin(), bytes.end());  // rewritten after every segment; the last one wins
        } else if (name == kInitName) {
            gHls->init.assign(bytes.begin(), bytes.end());
        } else {
            gHls->framed.file(name, bytes);
            gHls->segments.push_back({{"name", name}, {"bytes", bytes.size()}});
        }
    } catch (...) {
        if (gHls) gHls->error = std::current_exception();
        rc = AVERROR(EIO);
    }
    av_free(data);
    return rc;
}

}

nlohmann::json transcode(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink) {
    using namespace encode_detail;
    const Common c = parseCommon(args);
    Deadline deadline(c.deadline);
    Input in(range, deadline);
    checkDuration(in, c.maxDuration);
    Transcoder tx(in, *c.profile, c.hwaccel, c.threads, deadline, 0);

    SinkIo io(sink);
    AVFormatContext* raw = nullptr;
    if (avformat_alloc_output_context2(&raw, nullptr, "mp4", nullptr) < 0 || !raw) throw std::runtime_error("mp4 muxer unavailable");
    OutputContextPtr out(raw);
    out->pb = io.avio();
    out->flags |= AVFMT_FLAG_CUSTOM_IO;
    out->interrupt_callback = deadline.callback();
    tx.addStreams(out.get());

    const std::function<void(int, const char*)> muxFailed = [&](const int rc, const char* what) {
        io.rethrowStored();
        if (rc == AVERROR_EXIT) throw helpers::LimitExceeded("deadline exceeded");
        throw std::runtime_error(std::string("mp4 ") + what + ": " + avError(rc));
    };

    AVDictionary* opts = nullptr;
    if (tx.hasVideo()) {
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    } else {
        av_dict_set(&opts, "movflags", "empty_moov+default_base_moof", 0);
        av_dict_set(&opts, "frag_duration", "2000000", 0);
    }
    const int hrc = avformat_write_header(out.get(), &opts);
    av_dict_free(&opts);
    if (hrc < 0) muxFailed(hrc, "header");

    tx.run(out.get(), muxFailed);
    if (const int rc = av_write_trailer(out.get()); rc < 0) muxFailed(rc, "trailer");
    io.finish();

    nlohmann::json result = tx.describe();
    result["format"] = "mp4";
    result["fragmented"] = true;
    result["profile"] = c.profile->name;
    result["hwaccel_requested"] = c.hwaccel;
    const auto duration = in.durationSeconds();
    result["source_duration_seconds"] = duration ? nlohmann::json(*duration) : nlohmann::json();
    return result;
}

nlohmann::json hls(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink) {
    using namespace encode_detail;
    const Common c = parseCommon(args);
    const double segmentSeconds = numberOption(args, "segment-seconds", 6, 1, 30);
    Deadline deadline(c.deadline);
    Input in(range, deadline);
    checkDuration(in, c.maxDuration);
    Transcoder tx(in, *c.profile, c.hwaccel, c.threads, deadline, segmentSeconds);

    HlsCollector collector(sink);
    gHls = &collector;
    struct Reset { ~Reset() { gHls = nullptr; } } reset;

    AVFormatContext* raw = nullptr;
    if (avformat_alloc_output_context2(&raw, nullptr, "hls", std::string(kPlaylistName).c_str()) < 0 || !raw)
        throw std::runtime_error("hls muxer unavailable");
    OutputContextPtr out(raw);
    out->io_open = &hlsOpen;
    out->io_close2 = &hlsClose;
    out->interrupt_callback = deadline.callback();
    tx.addStreams(out.get());

    const std::function<void(int, const char*)> muxFailed = [&](const int rc, const char* what) {
        if (collector.error) std::rethrow_exception(collector.error);
        if (rc == AVERROR_EXIT) throw helpers::LimitExceeded("deadline exceeded");
        throw std::runtime_error(std::string("hls ") + what + ": " + avError(rc));
    };

    const std::string segTime = std::to_string(segmentSeconds);
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "hls_time", segTime.c_str(), 0);
    av_dict_set(&opts, "hls_list_size", "0", 0);
    av_dict_set(&opts, "hls_playlist_type", "vod", 0);
    av_dict_set(&opts, "hls_segment_type", "fmp4", 0);
    av_dict_set(&opts, "hls_fmp4_init_filename", std::string(kInitName).c_str(), 0);
    av_dict_set(&opts, "hls_segment_filename", "seg-%05d.m4s", 0);
    av_dict_set(&opts, "hls_flags", "independent_segments", 0);
    const int hrc = avformat_write_header(out.get(), &opts);
    av_dict_free(&opts);
    if (hrc < 0) muxFailed(hrc, "header");

    tx.run(out.get(), muxFailed);
    if (const int rc = av_write_trailer(out.get()); rc < 0) muxFailed(rc, "trailer");
    if (collector.error) std::rethrow_exception(collector.error);
    if (collector.init.empty()) throw std::runtime_error("hls muxer produced no init segment");
    if (collector.playlist.empty()) throw std::runtime_error("hls muxer produced no playlist");
    if (collector.segments.empty()) throw helpers::InvalidInput("no media segments produced");

    collector.framed.file(kInitName, collector.init);
    collector.framed.file(kPlaylistName, collector.playlist);  // written last: its presence means complete
    collector.framed.finish();

    nlohmann::json result = tx.describe();
    result["format"] = "hls";
    result["segment_type"] = "fmp4";
    result["segment_seconds"] = segmentSeconds;
    result["playlist"] = kPlaylistName;
    result["init"] = kInitName;
    result["segments"] = collector.segments;
    result["profile"] = c.profile->name;
    result["hwaccel_requested"] = c.hwaccel;
    const auto duration = in.durationSeconds();
    result["source_duration_seconds"] = duration ? nlohmann::json(*duration) : nlohmann::json();
    return result;
}

}
