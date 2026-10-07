// vaulthalla-preview-cad: STEP/STP -> GLB converter helper. Spawned by the daemon (preview::derive::Runner), one
// process per conversion, confined by rlimits (daemon) plus Landlock and seccomp (self-applied in runMain).
// Single-threaded (the mesher runs with isInParallel = false), so its seccomp filter denies clone outright.

#include "convert_step.hpp"
#include "common/protocol.hpp"

int main(int argc, char** argv) {
    namespace h = vh::helpers;
    return h::runMain(argc, argv, [](const h::Args& args, h::RangeClient& input, h::OutputSink& output) {
        if (args.command == "convert-step") return h::cad::convertStep(args, input, output);
        if (args.command == "selftest-sandbox") return h::cad::selftestSandbox(args);
        throw h::Unsupported("unknown command '" + args.command + "'");
    }, [](const h::Args&) {
        h::sandbox::Options options;
        options.allowThreads = false;
        return options;
    });
}
