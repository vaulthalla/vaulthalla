#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"
#include "CommandUsage.hpp"

#include <paths.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <optional>
#include <pwd.h>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace vh;
using namespace vh::protocols::shell;

namespace {

namespace fs = std::filesystem;

constexpr auto* kDbUser = "vaulthalla";
constexpr auto* kDbName = "vaulthalla";
constexpr auto* kServiceUnit = "vaulthalla.service";
constexpr auto* kPendingDbPasswordFile = "/run/vaulthalla/db_password";

constexpr auto* kNginxTemplate = "/usr/share/vaulthalla/nginx/vaulthalla.conf";
constexpr auto* kNginxSiteAvailable = "/etc/nginx/sites-available/vaulthalla";
constexpr auto* kNginxSiteEnabled = "/etc/nginx/sites-enabled/vaulthalla";
constexpr auto* kNginxManagedMarker = "/var/lib/vaulthalla/nginx_site_managed";

struct ExecResult {
    int code = 1;
    std::string output;
};

struct ServiceIdentity {
    uid_t uid = 0;
    gid_t gid = 0;
};

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

static std::string sqlLiteral(const std::string& s) {
    std::string out{"'"};
    out.reserve(s.size() + 2);
    for (const char c : s) {
        if (c == '\'') out += "''";
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

static bool hasEffectiveRoot() {
    return ::geteuid() == 0;
}

static bool canUsePasswordlessSudo() {
    if (!commandExists("sudo")) return false;
    return runCapture("sudo -n true").code == 0;
}

static bool hasRootOrEquivalentPrivileges() {
    return hasEffectiveRoot() || canUsePasswordlessSudo();
}

static std::string privilegedPrefix() {
    if (hasEffectiveRoot()) return "";
    if (canUsePasswordlessSudo()) return "sudo -n ";
    return "";
}

static std::string formatFailure(const std::string& step, const ExecResult& result) {
    std::ostringstream msg;
    msg << step << " (exit " << result.code << ")";
    if (!result.output.empty()) msg << ": " << result.output;
    return msg.str();
}

static ExecResult psqlSql(const std::string& prefix, const std::string& db, const std::string& sql) {
    return runCapture(prefix + " -d " + shellQuote(db) + " -tAc " + shellQuote(sql));
}

static std::vector<fs::path> listSqlFilesSorted(const fs::path& dir) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".sql") files.push_back(entry.path());
    }

    std::ranges::sort(files, [](const fs::path& a, const fs::path& b) {
        return a.filename().string() < b.filename().string();
    });
    return files;
}

static std::string makeDbPassword(const size_t len = 48) {
    static constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<std::size_t> dist(0, alphabet.size() - 1);

    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) out.push_back(alphabet[dist(rng)]);
    return out;
}

static std::optional<std::string> resolveServiceIdentity(ServiceIdentity& identity) {
    const auto* user = ::getpwnam(kDbUser);
    if (!user) return "system user '" + std::string(kDbUser) + "' not found";

    identity.uid = user->pw_uid;
    identity.gid = user->pw_gid;

    if (const auto* group = ::getgrnam(kDbUser); group)
        identity.gid = group->gr_gid;

    return std::nullopt;
}

static std::optional<std::string> enforceDbPasswordFileState(const fs::path& pendingPath, const ServiceIdentity& identity) {
    struct stat st{};
    if (::stat(pendingPath.c_str(), &st) != 0)
        return "failed to stat pending password file: " + pendingPath.string();

    if ((st.st_uid != identity.uid || st.st_gid != identity.gid) &&
        ::chown(pendingPath.c_str(), identity.uid, identity.gid) != 0) {
        const auto sudoChown = runCapture("sudo -n chown " + std::string(kDbUser) + ":" + std::string(kDbUser) + " " + shellQuote(pendingPath.string()));
        if (sudoChown.code != 0)
            return "failed setting ownership to " + std::string(kDbUser) + ":" + std::string(kDbUser) + " on " + pendingPath.string();
    }

    if ((st.st_mode & 0777) != (S_IRUSR | S_IWUSR) && ::chmod(pendingPath.c_str(), S_IRUSR | S_IWUSR) != 0) {
        const auto sudoChmod = runCapture("sudo -n chmod 0600 " + shellQuote(pendingPath.string()));
        if (sudoChmod.code != 0)
            return "failed setting mode 0600 on " + pendingPath.string();
    }

    if (::stat(pendingPath.c_str(), &st) != 0)
        return "failed to restat pending password file: " + pendingPath.string();

    if (st.st_uid != identity.uid || st.st_gid != identity.gid)
        return "pending password file has wrong ownership; expected " + std::string(kDbUser) + ":" + std::string(kDbUser);
    if ((st.st_mode & 0777) != (S_IRUSR | S_IWUSR))
        return "pending password file has wrong mode; expected 0600";

    return std::nullopt;
}

static std::optional<std::string> writePendingDbPassword(const std::string& pass) {
    const fs::path pendingPath{kPendingDbPasswordFile};
    ServiceIdentity identity{};
    if (const auto identityError = resolveServiceIdentity(identity)) return identityError;

    std::error_code ec;
    fs::create_directories(pendingPath.parent_path(), ec);
    if (ec) return "failed creating runtime directory: " + ec.message();

    {
        std::ofstream out(pendingPath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return "failed writing pending password file: " + pendingPath.string();
        out << pass << "\n";
    }

    if (::chmod(pendingPath.c_str(), S_IRUSR | S_IWUSR) != 0)
        return "failed setting mode 0600 on " + pendingPath.string();

    return enforceDbPasswordFileState(pendingPath, identity);
}

static std::optional<std::string> choosePostgresPrefix() {
    std::vector<std::string> prefixes;

    if (hasEffectiveRoot() && commandExists("runuser"))
        prefixes.push_back("runuser -u postgres -- psql -X -v ON_ERROR_STOP=1");

    if (canUsePasswordlessSudo())
        prefixes.push_back("sudo -n -u postgres psql -X -v ON_ERROR_STOP=1");

    for (const auto& prefix : prefixes) {
        const auto probe = runCapture(prefix + " -d postgres -tAc " + shellQuote("SELECT 1;"));
        if (probe.code == 0 && trim(probe.output) == "1") return prefix;
    }

    return std::nullopt;
}

static std::optional<std::string> restartOrStartService(std::string& actionTaken) {
    if (!commandExists("systemctl"))
        return "systemctl is not available; cannot hand off DB bootstrap to runtime startup";

    const auto prefix = privilegedPrefix();
    if (prefix.empty())
        return "insufficient privileges for systemctl; run with root or passwordless sudo access";

    const auto active = runCapture(prefix + "systemctl --quiet is-active " + kServiceUnit);
    const bool isActive = active.code == 0;

    ExecResult serviceAction;
    if (isActive) {
        serviceAction = runCapture(prefix + "systemctl restart " + kServiceUnit);
        actionTaken = "restarted";
    } else {
        serviceAction = runCapture(prefix + "systemctl start " + kServiceUnit);
        actionTaken = "started";
    }

    if (serviceAction.code != 0)
        return formatFailure("failed to " + actionTaken + " " + kServiceUnit, serviceAction);

    return std::nullopt;
}

static bool hasNonNginxListenersOnWebPorts() {
    if (!commandExists("ss")) return false;

    const auto listeners = runCapture("ss -H -ltnp '( sport = :80 or sport = :443 )'");
    if (listeners.code != 0 || listeners.output.empty()) return false;

    std::istringstream in(listeners.output);
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed.find("nginx") == std::string::npos) return true;
    }
    return false;
}

static bool hasCustomNginxSitesEnabled() {
    const fs::path sitesEnabled{"/etc/nginx/sites-enabled"};
    if (!fs::exists(sitesEnabled) || !fs::is_directory(sitesEnabled)) return false;

    for (const auto& entry : fs::directory_iterator(sitesEnabled)) {
        const auto base = entry.path().filename().string();
        if (base.empty()) continue;
        if (base == "default" || base == "vaulthalla") continue;
        return true;
    }
    return false;
}

static bool isManagedSiteSymlinkTarget(const fs::path& linkPath) {
    if (!fs::is_symlink(linkPath)) return false;
    const auto target = fs::read_symlink(linkPath);
    return target == fs::path(kNginxSiteAvailable) || target == fs::path("../sites-available/vaulthalla");
}

static bool filesEqual(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (!fs::exists(a, ec) || !fs::exists(b, ec)) return false;
    if (fs::file_size(a, ec) != fs::file_size(b, ec)) return false;

    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    if (!fa.is_open() || !fb.is_open()) return false;

    std::array<char, 4096> ba{};
    std::array<char, 4096> bb{};
    while (fa && fb) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        if (fa.gcount() != fb.gcount()) return false;
        if (!std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin())) return false;
    }
    return true;
}

static bool isSetupMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"setup", cmd}, input);
}

static CommandResult handle_setup_db(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "db"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("setup db: requires super_admin role");

    if (!hasRootOrEquivalentPrivileges()) {
        return invalid(
            "setup db: requires root privileges or equivalent non-interactive sudo access "
            "for PostgreSQL/systemd integration steps");
    }

    const fs::path schemaPath = vh::paths::getPsqlSchemasPath();
    if (!fs::exists(schemaPath) || !fs::is_directory(schemaPath))
        return invalid("setup db: canonical schema path is missing: " + schemaPath.string());

    std::vector<fs::path> sqlFiles;
    try {
        sqlFiles = listSqlFilesSorted(schemaPath);
    } catch (const std::exception& e) {
        return invalid("setup db: failed scanning canonical schema path '" + schemaPath.string() + "': " + e.what());
    }
    if (sqlFiles.empty())
        return invalid("setup db: canonical schema path has no .sql files: " + schemaPath.string());

    if (!commandExists("psql"))
        return invalid("setup db: PostgreSQL client 'psql' is not installed");

    const auto postgresPrefix = choosePostgresPrefix();
    if (!postgresPrefix) {
        return invalid(
            "setup db: unable to run PostgreSQL admin commands as 'postgres'. "
            "Verify local PostgreSQL is installed/running and root-equivalent privileges are available."
        );
    }

    const auto roleState = psqlSql(
        *postgresPrefix, "postgres", "SELECT 1 FROM pg_roles WHERE rolname = " + sqlLiteral(kDbUser) + ";");
    if (roleState.code != 0) return invalid("setup db: " + formatFailure("failed querying PostgreSQL role state", roleState));
    const bool roleExists = trim(roleState.output) == "1";

    const auto dbState = psqlSql(
        *postgresPrefix, "postgres", "SELECT 1 FROM pg_database WHERE datname = " + sqlLiteral(kDbName) + ";");
    if (dbState.code != 0) return invalid("setup db: " + formatFailure("failed querying PostgreSQL database state", dbState));
    const bool dbExists = trim(dbState.output) == "1";

    bool createdRole = false;
    bool createdDb = false;
    std::string generatedPassword;

    if (!roleExists) {
        generatedPassword = makeDbPassword();
        const auto createRole = psqlSql(
            *postgresPrefix,
            "postgres",
            "CREATE ROLE " + std::string(kDbUser) + " LOGIN PASSWORD " + sqlLiteral(generatedPassword) + ";"
        );
        if (createRole.code != 0)
            return invalid("setup db: " + formatFailure("failed creating PostgreSQL role '" + std::string(kDbUser) + "'", createRole));
        createdRole = true;
    }

    if (!dbExists) {
        const auto createDb = psqlSql(
            *postgresPrefix, "postgres", "CREATE DATABASE " + std::string(kDbName) + " OWNER " + std::string(kDbUser) + ";");
        if (createDb.code != 0)
            return invalid("setup db: " + formatFailure("failed creating PostgreSQL database '" + std::string(kDbName) + "'", createDb));
        createdDb = true;
    }

    const auto grantDb = psqlSql(
        *postgresPrefix, "postgres", "GRANT ALL PRIVILEGES ON DATABASE " + std::string(kDbName) + " TO " + std::string(kDbUser) + ";");
    if (grantDb.code != 0) return invalid("setup db: " + formatFailure("failed granting database privileges", grantDb));

    const auto grantSchema = psqlSql(
        *postgresPrefix, kDbName, "GRANT USAGE, CREATE ON SCHEMA public TO " + std::string(kDbUser) + ";");
    if (grantSchema.code != 0) return invalid("setup db: " + formatFailure("failed granting schema privileges", grantSchema));

    if (createdRole) {
        if (const auto passSeedError = writePendingDbPassword(generatedPassword))
            return invalid("setup db: failed preparing pending DB password handoff file: " + *passSeedError);
    }

    std::string serviceAction;
    if (const auto serviceError = restartOrStartService(serviceAction)) {
        std::ostringstream error;
        error << "setup db: local DB bootstrap completed but runtime service handoff failed: " << *serviceError;
        if (createdRole)
            error << ". Pending password remains at " << kPendingDbPasswordFile << " for next successful service startup.";
        return invalid(error.str());
    }

    std::ostringstream out;
    out << "setup db: local PostgreSQL bootstrap complete\n";
    out << "  role: " << (createdRole ? "created" : "already existed") << " (" << kDbUser << ")\n";
    out << "  database: " << (createdDb ? "created" : "already existed") << " (" << kDbName << ")\n";
    out << "  canonical schema path: " << schemaPath.string() << " (validated)\n";
    if (createdRole)
        out << "  seeded runtime DB password: " << kPendingDbPasswordFile << " (owner/mode verified)\n";
    else
        out << "  seeded runtime DB password: unchanged (existing role/password path)\n";
    out << "  service: " << kServiceUnit << " " << serviceAction << "\n";
    out << "  migrations: delegated to normal runtime startup flow (SqlDeployer)";

    return ok(out.str());
}

static CommandResult handle_setup_nginx(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "nginx"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("setup nginx: requires super_admin role");

    if (!commandExists("nginx") || !fs::exists("/etc/nginx"))
        return invalid("setup nginx: nginx is not installed or /etc/nginx is missing");

    if (!fs::exists(kNginxTemplate))
        return invalid("setup nginx: packaged nginx template missing at " + std::string(kNginxTemplate));

    if (hasNonNginxListenersOnWebPorts())
        return invalid("setup nginx: detected non-nginx listeners on :80/:443; refusing automatic integration");

    if (hasCustomNginxSitesEnabled() && !fs::exists(kNginxSiteEnabled))
        return invalid("setup nginx: custom nginx site layout detected; refusing automatic integration");

    bool createdSiteFile = false;
    bool createdSiteLink = false;
    bool createdMarker = false;

    try {
        fs::create_directories("/etc/nginx/sites-available");
        fs::create_directories("/etc/nginx/sites-enabled");

        const fs::path templatePath{kNginxTemplate};
        const fs::path siteAvailPath{kNginxSiteAvailable};
        const fs::path siteEnabledPath{kNginxSiteEnabled};
        const fs::path markerPath{kNginxManagedMarker};

        if (fs::exists(siteAvailPath)) {
            if (!filesEqual(siteAvailPath, templatePath) && !fs::exists(markerPath))
                return invalid("setup nginx: existing site file differs and is not package-managed: " + siteAvailPath.string());
        } else {
            fs::copy_file(templatePath, siteAvailPath, fs::copy_options::none);
            createdSiteFile = true;
        }

        if (!fs::exists(markerPath)) {
            fs::create_directories(markerPath.parent_path());
            std::ofstream marker(markerPath, std::ios::out | std::ios::trunc);
            marker << "managed-by=vaulthalla\n";
            createdMarker = true;
        }

        if (fs::exists(siteEnabledPath) && !fs::is_symlink(siteEnabledPath))
            return invalid("setup nginx: target exists and is not a symlink: " + siteEnabledPath.string());

        if (fs::is_symlink(siteEnabledPath) && !isManagedSiteSymlinkTarget(siteEnabledPath))
            return invalid("setup nginx: existing symlink points outside Vaulthalla-managed site");

        if (!fs::exists(siteEnabledPath)) {
            fs::create_symlink(siteAvailPath, siteEnabledPath);
            createdSiteLink = true;
        }
    } catch (const std::exception& e) {
        return invalid("setup nginx: failed preparing site files: " + std::string(e.what()));
    }

    const auto nginxTest = runCapture("nginx -t");
    if (nginxTest.code != 0) {
        std::error_code ec;
        bool rollbackOk = true;

        if (createdSiteLink) {
            fs::remove(kNginxSiteEnabled, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }
        if (createdSiteFile) {
            fs::remove(kNginxSiteAvailable, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }
        if (createdMarker) {
            fs::remove(kNginxManagedMarker, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }

        std::ostringstream err;
        err << "setup nginx: nginx -t failed before reload";
        if (rollbackOk) {
            err << "; rolled back newly staged Vaulthalla nginx changes";
        } else {
            err << "; rollback was partial, manual cleanup may be required for:\n"
                << "  - " << kNginxSiteEnabled << "\n"
                << "  - " << kNginxSiteAvailable << "\n"
                << "  - " << kNginxManagedMarker;
        }
        if (!nginxTest.output.empty()) err << "\nnginx -t output: " << nginxTest.output;
        return invalid(err.str());
    }

    std::string reloadStatus = "not attempted (systemctl unavailable or nginx inactive)";
    if (commandExists("systemctl") && runCapture("systemctl --quiet is-active nginx.service").code == 0) {
        const auto reload = runCapture("systemctl reload nginx.service");
        if (reload.code != 0) {
            std::ostringstream err;
            err << "setup nginx: site/config changes were applied and nginx -t passed, "
                << "but nginx reload failed. Configuration was left in place.\n";
            err << "reload failure: " << formatFailure("systemctl reload nginx.service", reload);
            return invalid(err.str());
        }
        reloadStatus = "nginx reloaded";
    }

    std::ostringstream out;
    out << "setup nginx: Vaulthalla nginx integration configured\n";
    out << "  site file: " << (createdSiteFile ? "installed" : "already present") << " (" << kNginxSiteAvailable << ")\n";
    out << "  site link: " << (createdSiteLink ? "enabled" : "already enabled") << " (" << kNginxSiteEnabled << ")\n";
    out << "  reload: " << reloadStatus;
    return ok(out.str());
}

static CommandResult handle_setup(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    if (isSetupMatch("db", sub)) return handle_setup_db(subcall);
    if (isSetupMatch("nginx", sub)) return handle_setup_nginx(subcall);

    return invalid(call.constructFullArgs(), "Unknown setup subcommand: '" + std::string(sub) + "'");
}

} // namespace

void commands::registerSetupCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("setup"), handle_setup);
}
