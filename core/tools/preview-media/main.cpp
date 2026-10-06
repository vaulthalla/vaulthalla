// vaulthalla-preview-media — out-of-process media helper (derive seam). See Commands.hpp and the protocol in
// common/protocol.hpp. FFmpeg and hardware devices are initialised here, before runMain applies the sandbox.

#include "Commands.hpp"
#include "Ffmpeg.hpp"
#include "Hardware.hpp"

#include <string>
#include <string_view>

namespace vh::media {

namespace main_detail {

std::string argValue(const int argc, char** argv, const std::string_view key, std::string fallback) {
    const std::string flag = "--" + std::string(key);
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == flag && i + 1 < argc) return argv[i + 1];
        if (arg.starts_with(flag + "=")) return std::string(arg.substr(flag.size() + 1));
    }
    return fallback;
}

nlohmann::json dispatch(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink) {
    if (args.command == "probe") return probe(args, range, sink);
    if (args.command == "poster") return poster(args, range, sink);
    if (args.command == "transcode") return transcode(args, range, sink);
    if (args.command == "hls") return hls(args, range, sink);
    if (args.command == "capabilities") return capabilities(args, range, sink);
    throw helpers::Unsupported("unknown command: " + args.command);
}

}

}

int main(int argc, char** argv) {
    av_log_set_level(AV_LOG_ERROR);

    // Pre-sandbox: hardware device contexts load their driver libraries and open render nodes now.
    const std::string_view command = argc > 1 ? argv[1] : "";
    if (command == "capabilities") {
        vh::media::hw::prepare("auto");
    } else if (command == "transcode" || command == "hls") {
        const std::string hwaccel = vh::media::main_detail::argValue(argc, argv, "hwaccel", "software");
        if (vh::media::hw::validHwaccel(hwaccel)) vh::media::hw::prepare(hwaccel);
    }

    return vh::helpers::runMain(argc, argv, &vh::media::main_detail::dispatch);
}
