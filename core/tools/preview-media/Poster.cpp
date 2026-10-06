#include "Commands.hpp"
#include "Ffmpeg.hpp"

extern "C" {
#include <libavutil/imgutils.h>
}

#include <cmath>
#include <limits>

namespace vh::media {

namespace poster_detail {

constexpr int kMaxPacketsWithoutFrame = 4000;

CodecContextPtr openDecoder(const AVStream* st) {
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) throw helpers::Unsupported(std::string("no decoder for ") + avcodec_get_name(st->codecpar->codec_id));
    CodecContextPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx) throw std::bad_alloc();
    if (avcodec_parameters_to_context(ctx.get(), st->codecpar) < 0) throw helpers::InvalidInput("bad codec parameters");
    ctx->thread_count = 2;
    ctx->max_pixels = kMaxPixels;
    ctx->pkt_timebase = st->time_base;
    if (const int rc = avcodec_open2(ctx.get(), codec, nullptr); rc < 0)
        throw helpers::InvalidInput("could not open decoder: " + avError(rc));
    return ctx;
}

// Seeks to the keyframe at or before `target` and returns the first frame decoded from there.
FramePtr decodeFrom(const Input& in, const int index, const double target, const Deadline& deadline) {
    AVFormatContext* fmt = in.fmt();
    AVStream* st = fmt->streams[index];
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        fmt->streams[i]->discard = static_cast<int>(i) == index ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

    const int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    const int64_t ts = start + static_cast<int64_t>(target / av_q2d(st->time_base));
    if (avformat_seek_file(fmt, index, std::numeric_limits<int64_t>::min(), ts, ts, 0) < 0 && target > 0)
        return nullptr;

    auto dec = openDecoder(st);
    auto pkt = makePacket();
    auto frame = makeFrame();
    int sinceFrame = 0;
    bool flushed = false;
    while (!flushed) {
        deadline.check();
        const int rc = av_read_frame(fmt, pkt.get());
        if (rc == AVERROR_EXIT) in.raise(rc, "read");
        if (rc < 0) {
            in.rethrowStored();
            avcodec_send_packet(dec.get(), nullptr);
            flushed = true;
        } else {
            const bool ours = pkt->stream_index == index;
            if (ours) avcodec_send_packet(dec.get(), pkt.get());  // corrupt packets are skipped
            av_packet_unref(pkt.get());
            if (!ours) continue;
        }
        const int got = avcodec_receive_frame(dec.get(), frame.get());
        if (got >= 0) return frame;
        if (got != AVERROR(EAGAIN) && got != AVERROR_EOF && got != AVERROR_INVALIDDATA) break;
        if (++sinceFrame > kMaxPacketsWithoutFrame) break;
    }
    return nullptr;
}

FramePtr decodeAttached(const AVStream* st) {
    auto dec = openDecoder(st);
    if (avcodec_send_packet(dec.get(), &st->attached_pic) < 0) return nullptr;
    avcodec_send_packet(dec.get(), nullptr);
    auto frame = makeFrame();
    if (avcodec_receive_frame(dec.get(), frame.get()) < 0) return nullptr;
    return frame;
}

FramePtr allocFrame(const int width, const int height, const AVPixelFormat format) {
    auto f = makeFrame();
    f->width = width;
    f->height = height;
    f->format = format;
    if (av_frame_get_buffer(f.get(), 0) < 0) throw std::bad_alloc();
    return f;
}

void rotatePlane(const uint8_t* src, const int srcStride, const int w, const int h, uint8_t* dst, const int dstStride,
                 const int degrees) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t v = src[y * srcStride + x];
            if (degrees == 90) dst[x * dstStride + (h - 1 - y)] = v;
            else if (degrees == 270) dst[(w - 1 - x) * dstStride + y] = v;
            else dst[(h - 1 - y) * dstStride + (w - 1 - x)] = v;
        }
    }
}

FramePtr rotate(const AVFrame* src, const int degrees) {
    const bool swap = degrees == 90 || degrees == 270;
    auto dst = allocFrame(swap ? src->height : src->width, swap ? src->width : src->height, AV_PIX_FMT_YUV420P);
    for (int p = 0; p < 3; ++p) {
        const int w = p == 0 ? src->width : src->width / 2;
        const int h = p == 0 ? src->height : src->height / 2;
        rotatePlane(src->data[p], src->linesize[p], w, h, dst->data[p], dst->linesize[p], degrees);
    }
    return dst;
}

FramePtr scale(const AVFrame* src, const int width, const int height) {
    const auto srcFormat = static_cast<AVPixelFormat>(src->format);
    SwsPtr sws(sws_getContext(src->width, src->height, srcFormat, width, height, AV_PIX_FMT_YUV420P,
                              SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr));
    if (!sws) throw helpers::Unsupported("cannot convert pixel format");
    const int srcSpace = src->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
    const int srcFull = src->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(srcSpace), srcFull, sws_getCoefficients(SWS_CS_DEFAULT), 1,
                             0, 1 << 16, 1 << 16);
    auto dst = allocFrame(width, height, AV_PIX_FMT_YUV420P);
    sws_scale(sws.get(), src->data, src->linesize, 0, src->height, dst->data, dst->linesize);
    return dst;
}

std::vector<uint8_t> encodeJpeg(AVFrame* frame) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) throw helpers::Unsupported("mjpeg encoder not in this FFmpeg build");
    CodecContextPtr enc(avcodec_alloc_context3(codec));
    if (!enc) throw std::bad_alloc();
    enc->width = frame->width;
    enc->height = frame->height;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->color_range = AVCOL_RANGE_JPEG;
    enc->time_base = AVRational{1, 25};
    enc->flags |= AV_CODEC_FLAG_QSCALE;
    enc->global_quality = FF_QP2LAMBDA * 3;
    if (const int rc = avcodec_open2(enc.get(), codec, nullptr); rc < 0)
        throw std::runtime_error("mjpeg encoder: " + avError(rc));

    frame->pts = 0;
    frame->quality = enc->global_quality;
    frame->color_range = AVCOL_RANGE_JPEG;
    if (const int rc = avcodec_send_frame(enc.get(), frame); rc < 0) throw std::runtime_error("mjpeg encode: " + avError(rc));
    avcodec_send_frame(enc.get(), nullptr);
    std::vector<uint8_t> out;
    auto pkt = makePacket();
    while (avcodec_receive_packet(enc.get(), pkt.get()) >= 0) {
        out.insert(out.end(), pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt.get());
    }
    if (out.empty()) throw std::runtime_error("mjpeg encoder produced no data");
    return out;
}

int even(const double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); }

}

nlohmann::json poster(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink) {
    Deadline deadline(std::chrono::seconds(static_cast<int64_t>(numberOption(args, "deadline-seconds", 120, 1, 86400))));
    const int maxWidth = static_cast<int>(numberOption(args, "max-width", 1280, 16, 4096));
    const std::string at = option(args, "at", "auto");
    const double requested = at == "auto" ? -1 : numberOption(args, "at", 0, 0, 1e9);

    Input in(range, deadline);
    const int video = in.bestVideoStream();
    const int attached = in.attachedPictureStream();
    if (video < 0 && attached < 0) throw helpers::Unsupported("no video stream or cover art");

    double target = 0;
    FramePtr frame;
    std::string source;
    int index = -1;
    int rotation = 0;
    if (video >= 0) {
        const auto duration = in.durationSeconds();
        if (requested >= 0) target = duration ? std::min(requested, *duration * 0.95) : requested;
        else target = duration ? *duration * 0.10 : 3.0;
        frame = poster_detail::decodeFrom(in, video, target, deadline);
        if (!frame && target > 0) {
            target = 0;
            frame = poster_detail::decodeFrom(in, video, 0, deadline);
        }
        if (frame) {
            source = "video";
            index = video;
            rotation = rotationDegrees(in.fmt()->streams[video]);
        }
    }
    if (!frame && attached >= 0) {
        frame = poster_detail::decodeAttached(in.fmt()->streams[attached]);
        if (frame) {
            source = "attached_pic";
            index = attached;
            target = 0;
        }
    }
    if (!frame) throw helpers::InvalidInput("no decodable video frame");
    if (frame->width <= 0 || frame->height <= 0 || frame->width > kMaxDimension || frame->height > kMaxDimension)
        throw helpers::InvalidInput("decoded frame has invalid dimensions");

    // Display size: sample aspect ratio, then rotation.
    double displayW = frame->width;
    const double displayH = frame->height;
    if (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0)
        displayW *= av_q2d(frame->sample_aspect_ratio);
    const bool swap = rotation == 90 || rotation == 270;
    const double shownW = swap ? displayH : displayW;
    const double shownH = swap ? displayW : displayH;
    const int outW = poster_detail::even(std::min<double>(maxWidth, shownW));
    const int outH = poster_detail::even(shownH * outW / shownW);

    auto scaled = poster_detail::scale(frame.get(), swap ? outH : outW, swap ? outW : outH);
    if (rotation != 0) scaled = poster_detail::rotate(scaled.get(), rotation);
    const auto jpeg = poster_detail::encodeJpeg(scaled.get());
    sink.write(jpeg);

    nlohmann::json result = {{"format", "jpeg"},   {"width", outW},          {"height", outH},
                             {"source", source},   {"stream_index", index},  {"rotation", rotation},
                             {"at_seconds", target}};
    if (frame->best_effort_timestamp != AV_NOPTS_VALUE && source == "video")
        result["frame_seconds"] = toSeconds(frame->best_effort_timestamp, in.fmt()->streams[index]->time_base);
    return result;
}

}
