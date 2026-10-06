#include "Commands.hpp"
#include "Ffmpeg.hpp"
#include "Hardware.hpp"

namespace vh::media {

namespace capabilities_detail {

std::string version(const unsigned v) {
    return std::to_string(AV_VERSION_MAJOR(v)) + "." + std::to_string(AV_VERSION_MINOR(v)) + "." +
           std::to_string(AV_VERSION_MICRO(v));
}

}

nlohmann::json capabilities(const helpers::Args&, helpers::RangeClient&, helpers::OutputSink& sink) {
    nlohmann::json doc;
    doc["helper"] = "vaulthalla-preview-media";
    doc["commands"] = {"probe", "poster", "transcode", "hls", "capabilities"};
    doc["profiles"] = {"h264-480", "h264-720", "h264-1080"};
    doc["hwaccel_modes"] = {"auto", "software", "vaapi", "qsv", "nvenc"};
    doc["libraries"] = {{"libavformat", capabilities_detail::version(avformat_version())},
                        {"libavcodec", capabilities_detail::version(avcodec_version())},
                        {"libavutil", capabilities_detail::version(avutil_version())},
                        {"libswscale", capabilities_detail::version(swscale_version())},
                        {"libswresample", capabilities_detail::version(swresample_version())}};

    nlohmann::json encoders = nlohmann::json::object();
    for (const char* name : {"libx264", "h264_vaapi", "h264_qsv", "h264_nvenc", "aac", "mjpeg"})
        encoders[name] = avcodec_find_encoder_by_name(name) != nullptr;
    doc["encoders"] = std::move(encoders);

    nlohmann::json decoders = nlohmann::json::object();
    for (const AVCodecID id : {AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_VP8, AV_CODEC_ID_VP9, AV_CODEC_ID_AV1,
                               AV_CODEC_ID_MPEG4, AV_CODEC_ID_MPEG2VIDEO, AV_CODEC_ID_THEORA, AV_CODEC_ID_PRORES,
                               AV_CODEC_ID_AAC, AV_CODEC_ID_MP3, AV_CODEC_ID_OPUS, AV_CODEC_ID_VORBIS, AV_CODEC_ID_FLAC,
                               AV_CODEC_ID_ALAC, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3, AV_CODEC_ID_PCM_S16LE})
        decoders[avcodec_get_name(id)] = avcodec_find_decoder(id) != nullptr;
    doc["decoders"] = std::move(decoders);

    doc["software_h264"] = avcodec_find_encoder_by_name("libx264") != nullptr;
    doc["hardware"] = hw::describe();
    // Device creation is probed; hardware *encoding* has not been validated on real VAAPI/QSV/NVENC hosts.
    doc["hardware_validated"] = false;

    const std::string body = doc.dump() + "\n";
    sink.write(std::span(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
    return doc;
}

}
