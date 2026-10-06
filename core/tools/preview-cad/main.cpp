// vaulthalla-preview-cad: STEP/STP -> GLB converter helper. Spawned by the daemon (preview::derive::Runner), one
// process per conversion, confined by rlimits (daemon) plus Landlock and seccomp (self-applied in runMain).

#include "convert_step.hpp"
#include "common/protocol.hpp"

int main(int argc, char** argv) {
    namespace h = vh::helpers;
    return h::runMain(argc, argv, [](const h::Args& args, h::RangeClient& input, h::OutputSink& output) {
        if (args.command == "convert-step") return h::cad::convertStep(args, input, output);
        if (args.command == "selftest-sandbox") return h::cad::selftestSandbox(args);
        throw h::Unsupported("unknown command '" + args.command + "'");
    });
}
