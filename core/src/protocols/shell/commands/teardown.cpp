#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/ExecResult.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"
#include "identities/User.hpp"
#include "CommandUsage.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>

using namespace vh;
using namespace vh::protocols::shell;

constexpr auto* kNginxSiteAvailable = "/etc/nginx/sites-available/vaulthalla";
constexpr auto* kNginxSiteEnabled = "/etc/nginx/sites-enabled/vaulthalla";
constexpr auto* kNginxManagedMarker = "/var/lib/vaulthalla/nginx_site_managed";

static std::string trim(std::string s) {
    const auto ws = [](const unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](const unsigned char c) { return !ws(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](const unsigned char c) { return !ws(c); }).base(), s.end());
    return s;
}

static std::string shellQuote(const std::string& s) {
    std::string out{"'"};
    for (const char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static ExecResult runCapture(const std::string& command) {
    const auto wrapped = command + " 2>&1";
    std::array<char, 4096> buf{};
    std::string output;

    FILE* pipe = ::popen(wrapped.c_str(), "r");
    if (!pipe) return {.code = 1, .output = "failed to execute command"};

    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
        output += buf.data();

    const int status = ::pclose(pipe);
    int code = status;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);
    return {.code = code, .output = trim(output)};
}

static bool commandExists(const std::string& command) {
    return runCapture("command -v " + shellQuote(command) + " >/dev/null").code == 0;
}

static bool isManagedSiteSymlinkTarget(const std::filesystem::path& linkPath) {
    if (!std::filesystem::is_symlink(linkPath)) return false;
    const auto target = std::filesystem::read_symlink(linkPath);
    return target == std::filesystem::path(kNginxSiteAvailable) || target == std::filesystem::path("../sites-available/vaulthalla");
}

static bool isTeardownMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"teardown", cmd}, input);
}

static CommandResult handle_teardown_nginx(const CommandCall& call) {
    const auto usage = resolveUsage({"teardown", "nginx"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("teardown nginx: requires super_admin role");

    bool removedLink = false;
    bool removedSiteFile = false;
    bool removedMarker = false;

    try {
        const std::filesystem::path siteEnabledPath{kNginxSiteEnabled};
        const std::filesystem::path siteAvailablePath{kNginxSiteAvailable};
        const std::filesystem::path markerPath{kNginxManagedMarker};

        if (std::filesystem::exists(siteEnabledPath)) {
            if (!std::filesystem::is_symlink(siteEnabledPath))
                return invalid("teardown nginx: " + siteEnabledPath.string() + " exists and is not a symlink");
            if (!isManagedSiteSymlinkTarget(siteEnabledPath))
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

    if (commandExists("nginx") && commandExists("systemctl") && runCapture("systemctl --quiet is-active nginx.service").code == 0) {
        const auto testResult = runCapture("nginx -t");
        if (testResult.code != 0)
            return invalid("teardown nginx: nginx -t failed after teardown: " + testResult.output);

        const auto reloadResult = runCapture("systemctl reload nginx.service");
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

static CommandResult handle_teardown(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isTeardownMatch("nginx", sub)) return handle_teardown_nginx(subcall);

    return invalid(call.constructFullArgs(), "Unknown teardown subcommand: '" + std::string(sub) + "'");
}

void commands::registerTeardownCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("teardown"), handle_teardown);
}
