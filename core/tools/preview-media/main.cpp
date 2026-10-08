// vaulthalla-preview-media — out-of-process media helper (derive seam). See Commands.hpp and the protocol in
// common/protocol.hpp. Hardware devices are opened only after runMain has applied the sandbox: drivers start
// threads, and landlock_restrict_self confines only the thread that calls it (seccomp's TSYNC would cover them,
// Landlock would not). The GPU allowance (render nodes, sysfs) is granted per invocation by sandboxFor.

#include "Commands.hpp"
#include "Ffmpeg.hpp"
#include "Hardware.hpp"

#include <string>
#include <string_view>

namespace vh::media {

namespace main_detail {

// The --hwaccel a command opens devices for: capabilities probes everything, transcode/hls their own choice.
std::string hwaccelFor(const helpers::Args& args) {
    if (args.command == "capabilities") return "auto";
    if (args.command == "transcode" || args.command == "hls") {
        const auto hwaccel = args.str("hwaccel", "software");
        if (hw::validHwaccel(hwaccel)) return hwaccel;
    }
    return "software";
}

helpers::sandbox::Options sandboxFor(const helpers::Args& args) {
    helpers::sandbox::Options options;
    options.allowGpuDevices = hwaccelFor(args) != "software";
    return options;
}

nlohmann::json dispatch(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink) {
    // Sandboxed by now: driver libraries load from /usr, render nodes open through the Landlock GPU allowance.
    if (const auto hwaccel = hwaccelFor(args); hwaccel != "software") hw::prepare(hwaccel);
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
    return vh::helpers::runMain(argc, argv, &vh::media::main_detail::dispatch, &vh::media::main_detail::sandboxFor);
}
