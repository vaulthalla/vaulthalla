#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/commandHelpers.hpp"
#include "protocols/shell/util/nginxHelpers.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

#include <exception>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

namespace vh::protocols::shell::commands {

namespace teardown_cmd_util = vh::protocols::shell::util;
namespace teardown_nginx_util = vh::protocols::shell::util;

namespace {

bool isTeardownMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"teardown", cmd}, input);
}

CommandResult handleTeardownNginx(const CommandCall& call) {
    const auto usage = resolveUsage({"teardown", "nginx"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("teardown nginx: requires super_admin role");

    bool removedLink = false;
    bool removedSiteFile = false;
    bool removedMarker = false;

    try {
        const std::filesystem::path siteEnabledPath{teardown_nginx_util::kNginxSiteEnabled};
        const std::filesystem::path siteAvailablePath{teardown_nginx_util::kNginxSiteAvailable};
        const std::filesystem::path markerPath{teardown_nginx_util::kNginxManagedMarker};

        if (std::filesystem::exists(siteEnabledPath)) {
            if (!std::filesystem::is_symlink(siteEnabledPath))
                return invalid("teardown nginx: " + siteEnabledPath.string() + " exists and is not a symlink");
            if (!teardown_nginx_util::isManagedSiteSymlinkTarget(siteEnabledPath))
                return invalid("teardown nginx: refusing to remove non-Vaulthalla nginx symlink target");
            std::filesystem::remove(siteEnabledPath);
            removedLink = true;
        }

        if (std::filesystem::exists(markerPath)) {
            if (std::filesystem::exists(siteAvailablePath)) {
                std::filesystem::remove(siteAvailablePath);
                removedSiteFile = true;
            }
            std::filesystem::remove(markerPath);
            removedMarker = true;
        }
    } catch (const std::exception& e) {
        return invalid("teardown nginx: failed removing Vaulthalla nginx integration: " + std::string(e.what()));
    }

    if (teardown_cmd_util::commandExists("nginx") &&
        teardown_cmd_util::commandExists("systemctl") &&
        teardown_cmd_util::runCapture("systemctl --quiet is-active nginx.service").code == 0) {
        const auto testResult = teardown_cmd_util::runCapture("nginx -t");
        if (testResult.code != 0)
            return invalid("teardown nginx: nginx -t failed after teardown: " + testResult.output);

        const auto reloadResult = teardown_cmd_util::runCapture("systemctl reload nginx.service");
        if (reloadResult.code != 0)
            return invalid("teardown nginx: failed reloading nginx.service: " + reloadResult.output);
    }

    std::ostringstream out;
    out << "teardown nginx: completed\n";
    out << "  removed enabled symlink: " << (removedLink ? "yes" : "no") << "\n";
    out << "  removed site file: " << (removedSiteFile ? "yes" : "no") << "\n";
    out << "  removed managed marker: " << (removedMarker ? "yes" : "no");
    return ok(out.str());
}

CommandResult handleTeardown(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isTeardownMatch("nginx", sub)) return handleTeardownNginx(subcall);

    return invalid(call.constructFullArgs(), "Unknown teardown subcommand: '" + std::string(sub) + "'");
}

}

void registerTeardownCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("teardown"), handleTeardown);
}

}
