#pragma once

// Direct-play classification for the major browsers. Pure (no FFmpeg, no I/O) so the daemon-side unit tests can
// exercise it without linking libav*. Inputs are FFmpeg codec names (avcodec_get_name) and a normalized container.
//
// The matrix is deliberately conservative and approximate (browser support moves; hardware-dependent decoders
// such as HEVC in Chrome/Edge or AV1 in Safari are not counted as direct play unless noted with low confidence).
// The web client still treats a <video> error event as the final word.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vh::media::browser {

enum class Browser { Chrome, Firefox, Safari };
enum class Support { No = 0, Low = 1, Medium = 2, High = 3 };

struct Traits {
    std::string container;               // normalized: mp4 mov webm matroska ogg mp3 flac wav adts avi mpegts …
    std::optional<std::string> video;    // primary video codec (attached pictures excluded)
    std::string videoPixFmt;             // e.g. yuv420p; empty when unknown
    std::string videoCodecTag;           // fourcc, e.g. avc1 hvc1 hev1; empty when unknown
    std::optional<std::string> audio;    // primary audio codec
};

struct Verdict {
    std::string verdict;                 // direct_play | container_remux_needed | video_transcode_needed |
                                         // audio_transcode_needed | transcode_needed | unsupported
    bool directPlay = false;
    Support confidence = Support::No;    // only meaningful for direct_play
    std::vector<std::string> reasons;
};

inline std::string_view toString(const Browser b) {
    switch (b) {
        case Browser::Chrome: return "chrome";
        case Browser::Firefox: return "firefox";
        default: return "safari";
    }
}

inline std::string_view toString(const Support s) {
    switch (s) {
        case Support::High: return "high";
        case Support::Medium: return "medium";
        case Support::Low: return "low";
        default: return "none";
    }
}

namespace detail {

inline bool isPcm(const std::string_view codec) { return codec.starts_with("pcm_"); }

inline Support containerSupport(const Browser b, const std::string_view c) {
    if (c == "mp4" || c == "mp3" || c == "adts" || c == "wav") return Support::High;
    if (c == "mov") return b == Browser::Safari ? Support::High : b == Browser::Chrome ? Support::Medium : Support::Low;
    if (c == "webm") return b == Browser::Safari ? Support::Medium : Support::High;
    if (c == "ogg") return b == Browser::Safari ? Support::Low : Support::High;
    if (c == "flac") return b == Browser::Safari ? Support::Medium : Support::High;
    return Support::No;  // matroska, avi, mpegts, flv, asf, …
}

inline Support videoSupport(const Browser b, const Traits& t, std::vector<std::string>& reasons) {
    const std::string_view v = *t.video;
    if (v == "h264") {
        const bool plain420 = t.videoPixFmt.empty() || t.videoPixFmt == "yuv420p" || t.videoPixFmt == "yuvj420p";
        if (plain420) return Support::High;
        reasons.emplace_back("h264_high_bit_depth_or_chroma");
        return b == Browser::Safari ? Support::Low : Support::No;
    }
    if (v == "hevc") {
        if (b != Browser::Safari) {
            reasons.emplace_back("hevc_safari_only");
            return Support::No;
        }
        if (t.videoCodecTag == "hvc1") return Support::High;
        reasons.emplace_back("hevc_tag_not_hvc1");
        return Support::Medium;
    }
    if (v == "vp8" || v == "vp9") return b == Browser::Safari ? Support::Medium : Support::High;
    if (v == "av1") {
        if (b != Browser::Safari) return Support::High;
        reasons.emplace_back("av1_safari_hardware_dependent");
        return Support::Low;
    }
    if (v == "theora") {
        if (b == Browser::Firefox) return Support::Medium;
        reasons.emplace_back("theora_firefox_only");
        return Support::No;
    }
    reasons.emplace_back("video_codec_not_browser_native");
    return Support::No;
}

inline Support audioSupport(const Browser b, const std::string_view a, std::vector<std::string>& reasons) {
    if (a == "aac" || a == "mp3" || isPcm(a)) return Support::High;
    if (a == "opus" || a == "flac") return b == Browser::Safari ? Support::Medium : Support::High;
    if (a == "vorbis") return b == Browser::Safari ? Support::Low : Support::High;
    if (a == "alac" || a == "ac3" || a == "eac3") {
        if (b == Browser::Safari) return Support::High;
        reasons.emplace_back("audio_codec_safari_only");
        return Support::No;
    }
    reasons.emplace_back("audio_codec_not_browser_native");
    return Support::No;
}

// Whether the container may legally carry the codec (a browser rejects e.g. AAC in WebM).
inline bool pairs(const std::string_view container, const std::optional<std::string>& video,
                  const std::optional<std::string>& audio) {
    auto in = [](const std::optional<std::string>& codec, std::initializer_list<std::string_view> allowed) {
        return !codec || std::ranges::find(allowed, std::string_view(*codec)) != allowed.end();
    };
    if (container == "mp4" || container == "mov")
        return in(video, {"h264", "hevc", "av1", "vp9"}) && in(audio, {"aac", "mp3", "opus", "flac", "alac", "ac3", "eac3"});
    if (container == "webm") return in(video, {"vp8", "vp9", "av1"}) && in(audio, {"vorbis", "opus"});
    if (container == "ogg") return in(video, {"theora", "vp8"}) && in(audio, {"vorbis", "opus", "flac"});
    if (container == "mp3") return !video && in(audio, {"mp3"});
    if (container == "adts") return !video && in(audio, {"aac"});
    if (container == "flac") return !video && in(audio, {"flac"});
    if (container == "wav") return !video && audio && isPcm(*audio);
    return false;
}

}

inline Verdict classify(const Browser b, const Traits& t) {
    Verdict out;
    if (!t.video && !t.audio) {
        out.verdict = "unsupported";
        out.reasons.emplace_back("no_audio_or_video_stream");
        return out;
    }
    const Support video = t.video ? detail::videoSupport(b, t, out.reasons) : Support::High;
    const Support audio = t.audio ? detail::audioSupport(b, *t.audio, out.reasons) : Support::High;
    Support container = detail::containerSupport(b, t.container);
    if (container == Support::No) out.reasons.emplace_back("container_not_browser_native");
    else if (!detail::pairs(t.container, t.video, t.audio)) {
        container = Support::No;
        out.reasons.emplace_back("codec_not_allowed_in_container");
    }

    if (video == Support::No && audio == Support::No) out.verdict = "transcode_needed";
    else if (video == Support::No) out.verdict = "video_transcode_needed";
    else if (audio == Support::No) out.verdict = "audio_transcode_needed";
    else if (container == Support::No) out.verdict = "container_remux_needed";
    else {
        out.verdict = "direct_play";
        out.directPlay = true;
        out.confidence = std::min({video, audio, container});
    }
    return out;
}

// {"chrome":{…},"firefox":{…},"safari":{…},"direct_play":["chrome",…],"action":"none|remux|transcode"}
// action is what the server should derive for the browsers that cannot direct-play.
inline nlohmann::json evaluate(const Traits& t) {
    nlohmann::json out = nlohmann::json::object();
    auto directPlay = nlohmann::json::array();
    bool needsTranscode = false, needsRemux = false;
    for (const Browser b : {Browser::Chrome, Browser::Firefox, Browser::Safari}) {
        const Verdict v = classify(b, t);
        nlohmann::json entry = {{"verdict", v.verdict}, {"direct_play", v.directPlay}, {"reasons", v.reasons}};
        if (v.directPlay) {
            entry["confidence"] = toString(v.confidence);
            directPlay.push_back(toString(b));
        } else if (v.verdict == "container_remux_needed") {
            needsRemux = true;
        } else {
            needsTranscode = true;
        }
        out[std::string(toString(b))] = std::move(entry);
    }
    out["direct_play"] = std::move(directPlay);
    out["action"] = needsTranscode ? "transcode" : needsRemux ? "remux" : "none";
    if (!t.video && !t.audio) out["action"] = "unsupported";
    return out;
}

// Reads the EBML DocType ("webm" or "matroska") from the head of a Matroska file; empty when absent.
inline std::string matroskaDocType(std::span<const uint8_t> head) {
    for (std::size_t i = 0; i + 3 < head.size(); ++i) {
        if (head[i] != 0x42 || head[i + 1] != 0x82) continue;
        const uint8_t size = head[i + 2];
        if (!(size & 0x80)) continue;  // only 1-byte vint sizes are used for DocType in practice
        const std::size_t len = size & 0x7f;
        if (len == 0 || len > 32 || i + 3 + len > head.size()) continue;
        std::string type(reinterpret_cast<const char*>(head.data() + i + 3), len);
        while (!type.empty() && type.back() == '\0') type.pop_back();
        return type;
    }
    return {};
}

// Maps an FFmpeg demuxer name (AVInputFormat::name) to a browser-facing container name.
inline std::string normalizeContainer(const std::string_view demuxer, const std::string_view majorBrand,
                                      const std::string_view matroskaDoc) {
    auto has = [&](const std::string_view token) {
        std::size_t start = 0;
        while (start <= demuxer.size()) {
            const auto end = std::min(demuxer.find(',', start), demuxer.size());
            if (demuxer.substr(start, end - start) == token) return true;
            start = end + 1;
        }
        return false;
    };
    if (has("mov") || has("mp4")) return majorBrand.starts_with("qt") ? "mov" : "mp4";
    if (has("matroska") || has("webm")) return matroskaDoc == "webm" ? "webm" : "matroska";
    if (has("aac")) return "adts";
    for (const std::string_view simple : {"ogg", "mp3", "flac", "wav", "avi", "mpegts", "flv", "asf", "mpeg"})
        if (has(simple)) return std::string(simple);
    return std::string(demuxer.substr(0, demuxer.find(',')));
}

}
