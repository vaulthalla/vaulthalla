#include "Hardware.hpp"
#include "Ffmpeg.hpp"

extern "C" {
#include <libavutil/hwcontext.h>
}

#include <algorithm>
#include <array>
#include <filesystem>
#include <system_error>

namespace vh::media::hw {

namespace hw_detail {

struct Slot {
    AVBufferRef* device = nullptr;
    std::string devicePath;
    std::string reason = "not probed";
    bool prepared = false;
};

std::array<Slot, 4>& slots() {
    static std::array<Slot, 4> s;
    return s;
}

Slot& slot(const Accel accel) { return slots()[static_cast<std::size_t>(accel)]; }

// Listed once, before the sandbox hides /dev.
const std::vector<std::string>& renderNodes() {
    static const std::vector<std::string> nodes = [] {
        std::vector<std::string> found;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator("/dev/dri", ec))
            if (entry.path().filename().string().starts_with("renderD")) found.push_back(entry.path().string());
        std::ranges::sort(found);
        return found;
    }();
    return nodes;
}

void create(const Accel accel) {
    Slot& s = slot(accel);
    if (s.prepared) return;
    s.prepared = true;
    if (!avcodec_find_encoder_by_name(encoderName(accel))) {
        s.reason = std::string("encoder ") + encoderName(accel) + " is not in this FFmpeg build";
        return;
    }

    int rc = AVERROR(ENODEV);
    switch (accel) {
        case Accel::Vaapi: {
            const auto& nodes = renderNodes();
            if (nodes.empty()) {
                s.reason = "no /dev/dri/renderD* node";
                return;
            }
            for (const auto& node : nodes) {
                rc = av_hwdevice_ctx_create(&s.device, AV_HWDEVICE_TYPE_VAAPI, node.c_str(), nullptr, 0);
                if (rc >= 0) {
                    s.devicePath = node;
                    break;
                }
            }
            break;
        }
        case Accel::Qsv:
            rc = av_hwdevice_ctx_create(&s.device, AV_HWDEVICE_TYPE_QSV, nullptr, nullptr, 0);
            break;
        case Accel::Nvenc:
            rc = av_hwdevice_ctx_create(&s.device, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
            break;
        case Accel::Software:
            s.reason.clear();
            return;
    }
    if (rc < 0) {
        s.device = nullptr;
        s.reason = "device creation failed: " + avError(rc);
    } else {
        s.reason.clear();
    }
}

}

std::string_view toString(const Accel accel) {
    switch (accel) {
        case Accel::Vaapi: return "vaapi";
        case Accel::Qsv: return "qsv";
        case Accel::Nvenc: return "nvenc";
        default: return "software";
    }
}

const char* encoderName(const Accel accel) {
    switch (accel) {
        case Accel::Vaapi: return "h264_vaapi";
        case Accel::Qsv: return "h264_qsv";
        case Accel::Nvenc: return "h264_nvenc";
        default: return "libx264";
    }
}

bool validHwaccel(const std::string_view hwaccel) {
    return hwaccel == "auto" || hwaccel == "software" || hwaccel == "vaapi" || hwaccel == "qsv" || hwaccel == "nvenc";
}

void prepare(const std::string_view hwaccel) {
    (void)hw_detail::renderNodes();
    for (const Accel a : {Accel::Vaapi, Accel::Qsv, Accel::Nvenc})
        if (hwaccel == "auto" || hwaccel == toString(a)) hw_detail::create(a);
}

AVBufferRef* device(const Accel accel) { return hw_detail::slot(accel).device; }

std::string unavailableReason(const Accel accel) {
    const auto& s = hw_detail::slot(accel);
    if (accel == Accel::Software) return {};
    if (!s.prepared) return "not requested";
    return s.reason;
}

std::vector<Accel> candidates(const std::string_view hwaccel) {
    std::vector<Accel> out;
    for (const Accel a : {Accel::Vaapi, Accel::Qsv, Accel::Nvenc})
        if (hwaccel == "auto" || hwaccel == toString(a)) out.push_back(a);
    out.push_back(Accel::Software);
    return out;
}

nlohmann::json describe() {
    nlohmann::json out = nlohmann::json::object();
    for (const Accel a : {Accel::Vaapi, Accel::Qsv, Accel::Nvenc}) {
        const auto& s = hw_detail::slot(a);
        nlohmann::json entry = {{"encoder", encoderName(a)},
                                {"encoder_in_build", avcodec_find_encoder_by_name(encoderName(a)) != nullptr},
                                {"device_available", s.device != nullptr}};
        entry["device"] = s.devicePath.empty() ? nlohmann::json() : nlohmann::json(s.devicePath);
        entry["reason"] = s.device ? nlohmann::json() : nlohmann::json(unavailableReason(a));
        out[std::string(toString(a))] = std::move(entry);
    }
    out["render_nodes"] = hw_detail::renderNodes();
    return out;
}

}
