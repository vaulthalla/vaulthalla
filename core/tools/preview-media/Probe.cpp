#include "Browser.hpp"
#include "Commands.hpp"
#include "Ffmpeg.hpp"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
}

#include <array>
#include <cmath>

namespace vh::media {

namespace probe_detail {

nlohmann::json nullableNumber(const double v) { return std::isfinite(v) && v > 0 ? nlohmann::json(v) : nlohmann::json(); }

std::string fourcc(const uint32_t tag) {
    if (tag == 0) return {};
    std::string out;
    for (int i = 0; i < 4; ++i) {
        const auto c = static_cast<char>((tag >> (8 * i)) & 0xff);
        out.push_back(c >= 0x20 && c < 0x7f ? c : '?');
    }
    return out;
}

nlohmann::json describeStream(AVFormatContext* fmt, AVStream* st) {
    const AVCodecParameters* par = st->codecpar;
    nlohmann::json s = {
        {"index", st->index},
        {"type", av_get_media_type_string(par->codec_type) ? av_get_media_type_string(par->codec_type) : "unknown"},
        {"codec_name", avcodec_get_name(par->codec_id)},
        {"codec_tag", fourcc(par->codec_tag)},
        {"default", (st->disposition & AV_DISPOSITION_DEFAULT) != 0},
        {"attached_pic", (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0},
    };
    const char* profile = avcodec_profile_name(par->codec_id, par->profile);
    s["profile"] = profile ? nlohmann::json(profile) : nlohmann::json();
    s["bit_rate"] = par->bit_rate > 0 ? nlohmann::json(par->bit_rate) : nlohmann::json();
    s["duration_seconds"] = st->duration != AV_NOPTS_VALUE ? nullableNumber(toSeconds(st->duration, st->time_base))
                                                          : nlohmann::json();
    const AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0);
    s["language"] = lang ? nlohmann::json(lang->value) : nlohmann::json();

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        const char* pix = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format));
        s["pix_fmt"] = pix ? nlohmann::json(pix) : nlohmann::json();
        s["width"] = par->width;
        s["height"] = par->height;
        const AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
        s["fps"] = fr.num > 0 && fr.den > 0 ? nullableNumber(av_q2d(fr)) : nlohmann::json();
        s["rotation"] = rotationDegrees(st);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        s["sample_rate"] = par->sample_rate;
        s["channels"] = par->ch_layout.nb_channels;
        std::array<char, 128> layout{};
        if (av_channel_layout_describe(&par->ch_layout, layout.data(), layout.size()) > 0) s["channel_layout"] = layout.data();
        else s["channel_layout"] = nullptr;
    }
    return s;
}

}

nlohmann::json probe(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink) {
    Deadline deadline(std::chrono::seconds(static_cast<int64_t>(numberOption(args, "deadline-seconds", 60, 1, 86400))));
    Input in(range, deadline);
    AVFormatContext* fmt = in.fmt();

    nlohmann::json doc;
    doc["format"] = fmt->iformat->name;
    doc["format_long_name"] = fmt->iformat->long_name ? fmt->iformat->long_name : "";
    doc["container"] = in.container();
    const auto duration = in.durationSeconds();
    doc["duration_seconds"] = duration ? nlohmann::json(*duration) : nlohmann::json();
    doc["bit_rate"] = fmt->bit_rate > 0 ? nlohmann::json(fmt->bit_rate) : nlohmann::json();
    doc["size"] = range.size();

    auto streams = nlohmann::json::array();
    for (unsigned i = 0; i < fmt->nb_streams; ++i) streams.push_back(probe_detail::describeStream(fmt, fmt->streams[i]));
    doc["streams"] = std::move(streams);

    const int video = in.bestVideoStream();
    const int audio = in.bestAudioStream();
    doc["video_stream"] = video >= 0 ? nlohmann::json(video) : nlohmann::json();
    doc["audio_stream"] = audio >= 0 ? nlohmann::json(audio) : nlohmann::json();
    doc["has_poster_source"] = video >= 0 || in.attachedPictureStream() >= 0;

    browser::Traits traits;
    traits.container = doc["container"].get<std::string>();
    if (video >= 0) {
        const AVCodecParameters* par = fmt->streams[video]->codecpar;
        traits.video = avcodec_get_name(par->codec_id);
        if (const char* pix = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format))) traits.videoPixFmt = pix;
        traits.videoCodecTag = probe_detail::fourcc(par->codec_tag);
    }
    if (audio >= 0) traits.audio = avcodec_get_name(fmt->streams[audio]->codecpar->codec_id);
    doc["browser"] = browser::evaluate(traits);

    const std::string body = doc.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    sink.write(std::span(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    return doc;
}

}
