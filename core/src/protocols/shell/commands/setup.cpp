#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/ExecResult.hpp"
#include "config/Config.hpp"
#include "db/query/identities/User.hpp"
#include "identities/User.hpp"
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

constexpr auto* kDbUser = "vaulthalla";
constexpr auto* kDbName = "vaulthalla";
constexpr auto* kServiceUnit = "vaulthalla.service";
constexpr auto* kPendingDbPasswordFile = "/run/vaulthalla/db_password";

constexpr auto* kNginxTemplate = "/usr/share/vaulthalla/nginx/vaulthalla.conf";
constexpr auto* kNginxSiteAvailable = "/etc/nginx/sites-available/vaulthalla";
constexpr auto* kNginxSiteEnabled = "/etc/nginx/sites-enabled/vaulthalla";
constexpr auto* kNginxManagedMarker = "/var/lib/vaulthalla/nginx_site_managed";
constexpr auto* kWebUpstreamHost = "127.0.0.1";
constexpr uint16_t kWebUpstreamPort = 36968;

struct ServiceIdentity {
    uid_t uid = 0;
    gid_t gid = 0;
};

struct CertbotPrereqState {
    bool certbot = false;
    bool nginxPlugin = false;
};

static std::string trim(std::string s) {
    const auto ws = [](const unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::ranges::find_if(s.begin(), s.end(), [&](const unsigned char c) { return !ws(c); }));
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

static std::vector<std::filesystem::path> listSqlFilesSorted(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".sql") files.push_back(entry.path());
    }

    std::ranges::sort(files, [](const std::filesystem::path& a, const std::filesystem::path& b) {
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

static std::optional<std::string> enforceDbPasswordFileState(const std::filesystem::path& pendingPath, const ServiceIdentity& identity) {
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
    const std::filesystem::path pendingPath{kPendingDbPasswordFile};
    ServiceIdentity identity{};
    if (const auto identityError = resolveServiceIdentity(identity)) return identityError;

    std::error_code ec;
    std::filesystem::create_directories(pendingPath.parent_path(), ec);
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
        prefixes.emplace_back("runuser -u postgres -- psql -X -v ON_ERROR_STOP=1");

    if (canUsePasswordlessSudo())
        prefixes.emplace_back("sudo -n -u postgres psql -X -v ON_ERROR_STOP=1");

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

static std::optional<std::string> requiredOptionValue(const CommandCall& call,
                                                      const std::shared_ptr<CommandUsage>& usage,
                                                      const std::string& label,
                                                      const std::string& prompt,
                                                      const bool interactive) {
    const auto required = usage->resolveRequired(label);
    if (!required) return std::nullopt;

    if (const auto value = optVal(call, required->option_tokens); value && !value->empty())
        return value;

    if (interactive && call.io)
        if (auto prompted = call.io->prompt(prompt); !prompted.empty())
            return prompted;

    return std::nullopt;
}

static std::optional<std::string> optionalOptionValue(const CommandCall& call,
                                                      const std::shared_ptr<CommandUsage>& usage,
                                                      const std::string& label,
                                                      const std::string& prompt,
                                                      const std::string& defValue,
                                                      const bool interactive) {
    const auto optional = usage->resolveOptional(label);
    if (!optional) return std::nullopt;

    if (const auto value = optVal(call, optional->option_tokens); value && !value->empty())
        return value;

    if (interactive && call.io)
        if (auto prompted = call.io->prompt(prompt, defValue); !prompted.empty())
            return prompted;

    if (!defValue.empty()) return defValue;
    return std::nullopt;
}

static std::optional<std::string> parsePortValue(const std::string& raw, uint16_t& port) {
    const auto parsed = parseUInt(raw);
    if (!parsed || *parsed == 0 || *parsed > 65535)
        return "invalid port '" + raw + "' (expected integer 1-65535)";
    port = static_cast<uint16_t>(*parsed);
    return std::nullopt;
}

static std::optional<std::string> parsePoolSizeValue(const std::optional<std::string>& raw, int& poolSize) {
    if (!raw || raw->empty()) return std::nullopt;
    const auto parsed = parseUInt(*raw);
    if (!parsed || *parsed == 0)
        return "invalid pool size '" + *raw + "' (expected positive integer)";
    poolSize = static_cast<int>(*parsed);
    return std::nullopt;
}

static std::optional<std::string> loadPasswordFromFile(const std::string& filePath, std::string& password) {
    const std::filesystem::path sourcePath{filePath};
    std::error_code ec;
    if (!std::filesystem::exists(sourcePath, ec))
        return "password file does not exist: " + sourcePath.string();
    if (!std::filesystem::is_regular_file(sourcePath, ec))
        return "password file is not a regular file: " + sourcePath.string();

    std::ifstream in(sourcePath);
    if (!in.is_open())
        return "failed opening password file: " + sourcePath.string();

    if (!(in >> password) || password.empty())
        return "password file is empty or invalid: " + sourcePath.string();

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
    const std::filesystem::path sitesEnabled{"/etc/nginx/sites-enabled"};
    if (!std::filesystem::exists(sitesEnabled) || !std::filesystem::is_directory(sitesEnabled)) return false;

    return std::ranges::any_of(std::filesystem::directory_iterator(sitesEnabled), [](const auto& entry) {
        const auto base = entry.path().filename().string();
        return !base.empty() && base != "default" && base != "vaulthalla";
    });
}

static bool isManagedSiteSymlinkTarget(const std::filesystem::path& linkPath) {
    if (!std::filesystem::is_symlink(linkPath)) return false;
    const auto target = std::filesystem::read_symlink(linkPath);
    return target == std::filesystem::path(kNginxSiteAvailable) || target == std::filesystem::path("../sites-available/vaulthalla");
}

static bool filesEqual(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (!std::filesystem::exists(a, ec) || !std::filesystem::exists(b, ec)) return false;
    if (std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec)) return false;

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

static bool tokenizedLineContainsDomain(const std::string& line, const std::string& domain) {
    std::istringstream in(line);
    std::string token;
    while (in >> token) {
        while (!token.empty() &&
               (token.back() == ',' || token.back() == ';' || token.back() == '.'))
            token.pop_back();
        if (token == domain) return true;
    }
    return false;
}

static bool certbotCertificatesMentionDomain(const std::string& output, const std::string& domain) {
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.rfind("Domains:", 0) != 0) continue;
        const auto domainsPart = trim(trimmed.substr(std::string("Domains:").size()));
        if (tokenizedLineContainsDomain(domainsPart, domain))
            return true;
    }
    return false;
}

static bool hasCertbotManagedCertForDomain(const std::string& domain) {
    if (const std::filesystem::path livePath = std::filesystem::path("/etc/letsencrypt/live") / domain;
        std::filesystem::exists(livePath / "fullchain.pem")
        && std::filesystem::exists(livePath / "privkey.pem"))
        return true;

    if (!commandExists("certbot")) return false;
    const auto [code, output] = runCapture("certbot certificates");
    if (code != 0) return false;
    return certbotCertificatesMentionDomain(output, domain);
}

static bool isLikelyDomain(const std::string& raw) {
    if (raw.empty() || raw.size() > 253) return false;
    if (raw.front() == '.' || raw.back() == '.' || raw.front() == '-' || raw.back() == '-') return false;

    bool hasDot = false;
    size_t labelLen = 0;
    for (const char c : raw) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.';
        if (!ok) return false;

        if (c == '.') {
            hasDot = true;
            if (labelLen == 0 || labelLen > 63) return false;
            labelLen = 0;
            continue;
        }

        ++labelLen;
    }

    if (labelLen == 0 || labelLen > 63) return false;
    return hasDot;
}

static bool containsListen80(const std::string& content) {
    return content.find("listen 80") != std::string::npos ||
           content.find("listen [::]:80") != std::string::npos;
}

static bool containsListen443(const std::string& content) {
    return content.find("listen 443") != std::string::npos ||
           content.find("listen [::]:443") != std::string::npos;
}

static bool looksCertbotManagedNginxConfig(const std::string& content) {
    return content.find("managed by Certbot") != std::string::npos ||
           content.find("/etc/letsencrypt/live/") != std::string::npos ||
           content.find("ssl_certificate ") != std::string::npos;
}

static std::optional<std::string> readFileToString(const std::filesystem::path& path, std::string& content) {
    std::ifstream in(path);
    if (!in.is_open()) return "failed opening file: " + path.string();
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return std::nullopt;
}

static std::optional<std::string> writeStringToFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return "failed opening file for write: " + path.string();
    out << content;
    if (!out.good()) return "failed writing file: " + path.string();
    return std::nullopt;
}

static std::string normalizedLocalUpstreamHost(const std::string& hostRaw) {
    auto host = trim(hostRaw);
    if (host.empty() || host == "0.0.0.0" || host == "::" || host == "[::]")
        return "127.0.0.1";
    return host;
}

static std::string hostForProxyPass(const std::string& hostRaw) {
    auto host = normalizedLocalUpstreamHost(hostRaw);
    if (host.find(':') != std::string::npos && !host.starts_with('['))
        return "[" + host + "]";
    return host;
}

static std::optional<std::string> renderManagedNginxConfig(const config::Config& cfg,
                                                           const std::optional<std::string>& domain,
                                                           std::string& rendered) {
    const auto serverName = domain && !domain->empty() ? *domain : "_";
    const auto wsHost = hostForProxyPass(cfg.websocket.host);
    const auto previewHost = hostForProxyPass(cfg.http_preview.host);

    std::ostringstream out;
    out << "# Generated by 'vh setup nginx'.\n";
    out << "# Do not edit manually; rerun CLI setup to apply managed changes.\n";
    out << "server {\n";
    out << "    listen 80;\n";
    out << "    listen [::]:80;\n";
    out << "    server_name " << serverName << ";\n\n";

    if (cfg.websocket.enabled) {
        out << "    location /ws {\n";
        out << "        proxy_pass http://" << wsHost << ":" << cfg.websocket.port << ";\n";
        out << "        proxy_http_version 1.1;\n";
        out << "        proxy_set_header Host $host;\n";
        out << "        proxy_set_header X-Real-IP $remote_addr;\n";
        out << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n";
        out << "        proxy_set_header X-Forwarded-Proto $scheme;\n";
        out << "        proxy_set_header Upgrade $http_upgrade;\n";
        out << "        proxy_set_header Connection \"upgrade\";\n";
        out << "    }\n\n";
    }

    if (cfg.http_preview.enabled) {
        out << "    location /preview {\n";
        out << "        proxy_pass http://" << previewHost << ":" << cfg.http_preview.port << ";\n";
        out << "        proxy_http_version 1.1;\n";
        out << "        proxy_set_header Host $host;\n";
        out << "        proxy_set_header X-Real-IP $remote_addr;\n";
        out << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n";
        out << "        proxy_set_header X-Forwarded-Proto $scheme;\n";
        out << "    }\n\n";
    }

    out << "    location / {\n";
    out << "        proxy_pass http://" << kWebUpstreamHost << ":" << kWebUpstreamPort << ";\n";
    out << "        proxy_http_version 1.1;\n";
    out << "        proxy_set_header Host $host;\n";
    out << "        proxy_set_header X-Real-IP $remote_addr;\n";
    out << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n";
    out << "        proxy_set_header X-Forwarded-Proto $scheme;\n";
    out << "        proxy_set_header Upgrade $http_upgrade;\n";
    out << "        proxy_set_header Connection \"upgrade\";\n";
    out << "    }\n";
    out << "}\n";

    rendered = out.str();
    return std::nullopt;
}

static CertbotPrereqState detectCertbotPrereqs() {
    CertbotPrereqState out;
    out.certbot = commandExists("certbot");
    if (!out.certbot) return out;

    const auto plugins = runCapture("certbot plugins");
    out.nginxPlugin = plugins.code == 0 && plugins.output.find("nginx") != std::string::npos;
    return out;
}

static std::optional<std::string> ensureCertbotPrereqs(const CommandCall& call, bool& installedNow) {
    installedNow = false;
    auto prereqs = detectCertbotPrereqs();
    if (prereqs.certbot && prereqs.nginxPlugin) return std::nullopt;

    std::vector<std::string> missing;
    if (!prereqs.certbot) missing.emplace_back("certbot");
    if (!prereqs.nginxPlugin) missing.emplace_back("python3-certbot-nginx");

    std::ostringstream missingMsg;
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i) missingMsg << ", ";
        missingMsg << missing[i];
    }

    if (!call.io) {
        return "certbot prerequisites missing in noninteractive context (" + missingMsg.str() +
               "). Install packages manually and rerun.";
    }

    const auto prompt = "setup nginx: certbot prerequisites missing (" + missingMsg.str() +
                        "). Install via apt-get now? [no]";
    const bool installNow = call.io->confirm(prompt, true);
    if (!installNow) {
        return "certbot prerequisites missing (" + missingMsg.str() +
               "). Install them manually and rerun.";
    }

    const bool aptSupported = std::filesystem::exists("/etc/debian_version") && commandExists("apt-get");
    if (!aptSupported) {
        return "automatic certbot prerequisite install is only supported on Debian/Ubuntu apt environments. "
               "Install packages manually and rerun.";
    }

    const auto prefix = privilegedPrefix();
    if (prefix.empty())
        return "insufficient privileges to install certbot prerequisites; rerun as root or with passwordless sudo";

    const auto install = runCapture(prefix + "DEBIAN_FRONTEND=noninteractive apt-get install -y certbot python3-certbot-nginx");
    if (install.code != 0)
        return formatFailure("failed installing certbot prerequisites", install);

    prereqs = detectCertbotPrereqs();
    if (!prereqs.certbot || !prereqs.nginxPlugin)
        return "certbot prerequisites still unavailable after install attempt";

    installedNow = true;
    return std::nullopt;
}

static std::optional<std::string> validateAndReloadNginx(std::string& reloadStatus) {
    const auto nginxTest = runCapture("nginx -t");
    if (nginxTest.code != 0) {
        if (!nginxTest.output.empty())
            return "nginx -t failed: " + nginxTest.output;
        return "nginx -t failed";
    }

    reloadStatus = "not attempted (systemctl unavailable or nginx inactive)";
    if (commandExists("systemctl") && runCapture("systemctl --quiet is-active nginx.service").code == 0) {
        const auto reload = runCapture("systemctl reload nginx.service");
        if (reload.code != 0)
            return formatFailure("systemctl reload nginx.service", reload);
        reloadStatus = "nginx reloaded";
    }

    return std::nullopt;
}

static bool isSetupMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"setup", cmd}, input);
}

static CommandResult handle_setup_assign_admin(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "assign-admin"});
    validatePositionals(call, usage);

    if (!call.user)
        return invalid("setup assign-admin: missing authenticated shell user context");

    if (!call.user->isSuperAdmin())
        return invalid("setup assign-admin: requires super_admin role");

    if (!call.user->meta.linux_uid.has_value())
        return invalid("setup assign-admin: current shell user has no Linux UID mapping");

    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin)
        return invalid("setup assign-admin: admin user record not found");

    const auto callerUid = *call.user->meta.linux_uid;
    if (admin->meta.linux_uid.has_value()) {
        if (*admin->meta.linux_uid == callerUid) {
            std::ostringstream out;
            out << "setup assign-admin: already assigned\n";
            out << "  admin linux_uid: " << callerUid << "\n";
            out << "  owner: " << call.user->name;
            return ok(out.str());
        }

        std::ostringstream err;
        err << "setup assign-admin: admin linux_uid is already bound to UID "
            << *admin->meta.linux_uid
            << " and does not match current operator UID "
            << callerUid
            << ". Refusing to rebind implicitly.";
        return invalid(err.str());
    }

    admin->meta.linux_uid = callerUid;
    admin->meta.updated_by = call.user->id;
    db::query::identities::User::updateUser(admin);

    std::ostringstream out;
    out << "setup assign-admin: assigned\n";
    out << "  admin linux_uid: " << callerUid << "\n";
    out << "  owner: " << call.user->name;
    return ok(out.str());
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

    const std::filesystem::path schemaPath = vh::paths::getPsqlSchemasPath();
    if (!std::filesystem::exists(schemaPath) || !std::filesystem::is_directory(schemaPath))
        return invalid("setup db: canonical schema path is missing: " + schemaPath.string());

    std::vector<std::filesystem::path> sqlFiles;
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

static CommandResult handle_setup_remote_db(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "remote-db"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("setup remote-db: requires super_admin role");

    if (!hasEffectiveRoot())
        return invalid("setup remote-db: must run as root to update config and seed runtime credentials");

    const auto interactiveFlag = usage->resolveFlag("interactive_mode");
    const bool interactive = interactiveFlag ? hasFlag(call, interactiveFlag->aliases) : hasFlag(call, "interactive");

    const auto host = requiredOptionValue(
        call, usage, "host", "Remote DB host (required):", interactive);
    if (!host || host->empty())
        return invalid("setup remote-db: missing required option --host");

    const auto user = requiredOptionValue(
        call, usage, "user", "Remote DB user (required):", interactive);
    if (!user || user->empty())
        return invalid("setup remote-db: missing required option --user");

    const auto database = requiredOptionValue(
        call, usage, "database", "Remote DB database name (required):", interactive);
    if (!database || database->empty())
        return invalid("setup remote-db: missing required option --database");

    const auto passwordFile = requiredOptionValue(
        call, usage, "password_file", "Remote DB password file path (required):", interactive);
    if (!passwordFile || passwordFile->empty())
        return invalid("setup remote-db: missing required option --password-file");

    const auto portRaw = optionalOptionValue(
        call, usage, "port", "Remote DB port [5432]:", "5432", interactive);
    if (!portRaw)
        return invalid("setup remote-db: missing port value");

    uint16_t port = 5432;
    if (const auto portErr = parsePortValue(*portRaw, port))
        return invalid("setup remote-db: " + *portErr);

    const auto poolRaw = optionalOptionValue(
        call, usage, "pool_size", "DB pool size (leave blank to keep current):", "", interactive);
    int parsedPoolSize = 0;
    if (const auto poolErr = parsePoolSizeValue(poolRaw, parsedPoolSize))
        return invalid("setup remote-db: " + *poolErr);

    std::string remotePassword;
    if (const auto passErr = loadPasswordFromFile(*passwordFile, remotePassword))
        return invalid("setup remote-db: " + *passErr);

    const std::filesystem::path configPath = vh::paths::getConfigPath();
    config::Config cfg;
    try {
        cfg = config::loadConfig(configPath.string());
    } catch (const std::exception& e) {
        return invalid("setup remote-db: failed loading config '" + configPath.string() + "': " + e.what());
    }

    cfg.database.host = *host;
    cfg.database.port = port;
    cfg.database.user = *user;
    cfg.database.name = *database;
    if (poolRaw && !poolRaw->empty()) cfg.database.pool_size = parsedPoolSize;

    try {
        cfg.save();
    } catch (const std::exception& e) {
        return invalid("setup remote-db: failed saving config '" + configPath.string() + "': " + e.what());
    }

    if (const auto passSeedError = writePendingDbPassword(remotePassword))
        return invalid(
            "setup remote-db: config updated but failed preparing pending DB password handoff file: " + *passSeedError);

    std::string serviceAction;
    if (const auto serviceError = restartOrStartService(serviceAction)) {
        std::ostringstream error;
        error << "setup remote-db: config updated but service handoff failed: " << *serviceError;
        error << ". Pending password remains at " << kPendingDbPasswordFile
              << " for next successful service startup.";
        return invalid(error.str());
    }

    std::ostringstream out;
    out << "setup remote-db: remote PostgreSQL configuration applied\n";
    out << "  config file: " << configPath.string() << "\n";
    out << "  database.host: " << cfg.database.host << "\n";
    out << "  database.port: " << cfg.database.port << "\n";
    out << "  database.user: " << cfg.database.user << "\n";
    out << "  database.name: " << cfg.database.name << "\n";
    out << "  database.pool_size: " << cfg.database.pool_size << "\n";
    out << "  seeded runtime DB password: " << kPendingDbPasswordFile << " (owner/mode verified)\n";
    out << "  service: " << kServiceUnit << " " << serviceAction << "\n";
    out << "  migrations: delegated to normal runtime startup flow (SqlDeployer)";
    return ok(out.str());
}

static CommandResult handle_setup_nginx(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "nginx"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("setup nginx: requires super_admin role");

    if (!hasEffectiveRoot())
        return invalid("setup nginx: must run as root to manage /etc/nginx and certbot state safely");

    const auto certbotFlag = usage->resolveFlag("certbot_mode");
    const bool useCertbot = certbotFlag ? hasFlag(call, certbotFlag->aliases) : hasFlag(call, "certbot");

    const auto domainOptDef = usage->resolveOptional("domain");
    const auto domainOpt = domainOptDef ? optVal(call, domainOptDef->option_tokens) : std::nullopt;

    if (!useCertbot && domainOpt && !domainOpt->empty())
        return invalid("setup nginx: --domain requires --certbot");

    std::string certbotDomain;
    if (useCertbot) {
        if (!domainOpt || domainOpt->empty())
            return invalid("setup nginx: --certbot requires --domain <domain>");
        certbotDomain = trim(*domainOpt);
        if (!isLikelyDomain(certbotDomain))
            return invalid("setup nginx: invalid domain '" + certbotDomain + "'");
    }

    if (!commandExists("nginx") || !std::filesystem::exists("/etc/nginx"))
        return invalid("setup nginx: nginx is not installed or /etc/nginx is missing");

    if (hasNonNginxListenersOnWebPorts())
        return invalid("setup nginx: detected non-nginx listeners on :80/:443; refusing automatic integration");

    if (hasCustomNginxSitesEnabled() && !std::filesystem::exists(kNginxSiteEnabled))
        return invalid("setup nginx: custom nginx site layout detected; refusing automatic integration");

    const std::filesystem::path configPath = vh::paths::getConfigPath();
    config::Config runtimeConfig;
    try {
        runtimeConfig = config::loadConfig(configPath.string());
    } catch (const std::exception& e) {
        return invalid("setup nginx: failed loading runtime config '" + configPath.string() + "': " + e.what());
    }

    const bool hasExistingCert = useCertbot ? hasCertbotManagedCertForDomain(certbotDomain) : false;

    bool createdSiteFile = false;
    bool createdSiteLink = false;
    bool createdMarker = false;
    bool rewroteSiteFile = false;
    bool backedUpSiteFile = false;
    bool skippedRewriteForExistingCert = false;
    bool preservedCertbotManagedTls = false;

    const std::filesystem::path siteSetupBackupPath = std::filesystem::path(std::string(kNginxSiteAvailable) + ".vh-setup.backup");

    try {
        std::filesystem::create_directories("/etc/nginx/sites-available");
        std::filesystem::create_directories("/etc/nginx/sites-enabled");

        const std::filesystem::path templatePath{kNginxTemplate};
        const std::filesystem::path siteAvailPath{kNginxSiteAvailable};
        const std::filesystem::path siteEnabledPath{kNginxSiteEnabled};
        const std::filesystem::path markerPath{kNginxManagedMarker};

        const bool markerExists = std::filesystem::exists(markerPath);
        const auto domainForManagedConfig = useCertbot ? std::optional<std::string>(certbotDomain) : std::nullopt;

        std::string renderedManagedConfig;
        if (const auto renderError = renderManagedNginxConfig(runtimeConfig, domainForManagedConfig, renderedManagedConfig))
            return invalid("setup nginx: " + *renderError);

        if (std::filesystem::exists(siteAvailPath)) {
            if (!markerExists) {
                if (!std::filesystem::exists(templatePath))
                    return invalid(
                        "setup nginx: packaged nginx template missing at " + std::string(kNginxTemplate) +
                        " while existing unmanaged site file is present");
                if (!filesEqual(siteAvailPath, templatePath))
                    return invalid("setup nginx: existing site file differs and is not package-managed: " + siteAvailPath.string());
            }

            if (useCertbot && hasExistingCert && markerExists) {
                skippedRewriteForExistingCert = true;
            } else {
                std::string existingContent;
                if (const auto readErr = readFileToString(siteAvailPath, existingContent))
                    return invalid("setup nginx: " + *readErr);

                if (!useCertbot && markerExists && looksCertbotManagedNginxConfig(existingContent)) {
                    preservedCertbotManagedTls = true;
                } else if (existingContent != renderedManagedConfig) {
                    std::error_code ec;
                    std::filesystem::copy_file(siteAvailPath, siteSetupBackupPath, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)
                        return invalid("setup nginx: failed creating managed site backup: " + ec.message());
                    backedUpSiteFile = true;

                    if (const auto writeErr = writeStringToFile(siteAvailPath, renderedManagedConfig))
                        return invalid("setup nginx: " + *writeErr);
                    rewroteSiteFile = true;
                }
            }
        } else {
            if (const auto writeErr = writeStringToFile(siteAvailPath, renderedManagedConfig))
                return invalid("setup nginx: " + *writeErr);
            createdSiteFile = true;
            rewroteSiteFile = true;
        }

        if (!std::filesystem::exists(markerPath)) {
            std::filesystem::create_directories(markerPath.parent_path());
            std::ofstream marker(markerPath, std::ios::out | std::ios::trunc);
            if (!marker.is_open())
                return invalid("setup nginx: failed opening managed marker for write: " + markerPath.string());
            marker << "managed-by=vaulthalla\n";
            if (!marker.good())
                return invalid("setup nginx: failed writing managed marker: " + markerPath.string());
            marker.close();
            createdMarker = true;
        }

        if (std::filesystem::exists(siteEnabledPath) && !std::filesystem::is_symlink(siteEnabledPath))
            return invalid("setup nginx: target exists and is not a symlink: " + siteEnabledPath.string());

        if (std::filesystem::is_symlink(siteEnabledPath) && !isManagedSiteSymlinkTarget(siteEnabledPath))
            return invalid("setup nginx: existing symlink points outside Vaulthalla-managed site");

        if (!std::filesystem::exists(siteEnabledPath)) {
            std::filesystem::create_symlink(siteAvailPath, siteEnabledPath);
            createdSiteLink = true;
        }
    } catch (const std::exception& e) {
        return invalid("setup nginx: failed preparing site files: " + std::string(e.what()));
    }

    std::string reloadStatus;
    if (const auto validateError = validateAndReloadNginx(reloadStatus)) {
        std::error_code ec;
        bool rollbackOk = true;

        if (createdSiteLink) {
            std::filesystem::remove(kNginxSiteEnabled, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }
        if (createdSiteFile) {
            std::filesystem::remove(kNginxSiteAvailable, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        } else if (backedUpSiteFile) {
            std::filesystem::copy_file(siteSetupBackupPath, kNginxSiteAvailable, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }
        if (createdMarker) {
            std::filesystem::remove(kNginxManagedMarker, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }
        if (backedUpSiteFile) {
            std::filesystem::remove(siteSetupBackupPath, ec);
            ec.clear();
        }

        std::ostringstream err;
        err << "setup nginx: failed nginx validation/reload during managed site staging";
        if (rollbackOk) {
            err << "; rolled back newly staged Vaulthalla nginx changes";
        } else {
            err << "; rollback was partial, manual cleanup may be required for:\n"
                << "  - " << kNginxSiteEnabled << "\n"
                << "  - " << kNginxSiteAvailable << "\n"
                << "  - " << kNginxManagedMarker;
        }
        err << "\nfailure: " << *validateError;
        return invalid(err.str());
    }

    if (backedUpSiteFile) {
        std::error_code ec;
        std::filesystem::remove(siteSetupBackupPath, ec);
    }

    if (!useCertbot) {
        std::ostringstream out;
        out << "setup nginx: Vaulthalla nginx integration configured\n";
        out << "  site file: "
            << (createdSiteFile ? "installed" : rewroteSiteFile ? "regenerated from canonical config" : "already current")
            << " (" << kNginxSiteAvailable << ")\n";
        out << "  site link: " << (createdSiteLink ? "enabled" : "already enabled") << " (" << kNginxSiteEnabled << ")\n";
        out << "  config source: " << configPath.string() << "\n";
        out << "  websocket upstream: "
            << (runtimeConfig.websocket.enabled
                ? hostForProxyPass(runtimeConfig.websocket.host) + ":" + std::to_string(runtimeConfig.websocket.port)
                : "disabled in config") << "\n";
        out << "  preview upstream: "
            << (runtimeConfig.http_preview.enabled
                ? hostForProxyPass(runtimeConfig.http_preview.host) + ":" + std::to_string(runtimeConfig.http_preview.port)
                : "disabled in config") << "\n";
        out << "  web upstream: " << kWebUpstreamHost << ":" << kWebUpstreamPort << " (runtime convention)\n";
        if (preservedCertbotManagedTls)
            out << "  managed TLS config: preserved existing certbot-managed HTTPS directives\n";
        out << "  reload: " << reloadStatus;
        return ok(out.str());
    }

    if (!std::filesystem::exists(kNginxManagedMarker))
        return invalid("setup nginx: certbot mode requires Vaulthalla-managed nginx marker state");

    bool prereqsInstalledNow = false;
    if (const auto prereqError = ensureCertbotPrereqs(call, prereqsInstalledNow))
        return invalid("setup nginx: " + *prereqError);

    const std::filesystem::path siteAvailablePath{kNginxSiteAvailable};
    std::string certbotMode;
    std::string postCertReloadStatus = reloadStatus;

    if (hasExistingCert) {
        certbotMode = "existing certificate detected (renew-safe path)";
        const auto renew = runCapture("certbot renew --cert-name " + shellQuote(certbotDomain) + " --non-interactive");
        if (renew.code != 0) {
            std::ostringstream err;
            err << "setup nginx: existing certificate state detected for " << certbotDomain << ", "
                << "but certbot renew failed\n"
                << "renew failure: " << formatFailure("certbot renew --cert-name " + certbotDomain, renew);
            return invalid(err.str());
        }
    } else {
        certbotMode = "no existing certificate detected (fresh issuance path)";

        std::string currentContent;
        if (const auto readErr = readFileToString(siteAvailablePath, currentContent))
            return invalid("setup nginx: failed reading managed site before certbot issuance: " + *readErr);

        const bool had80 = containsListen80(currentContent);
        const bool had443 = containsListen443(currentContent);

        const std::filesystem::path backupPath = std::filesystem::path(std::string(kNginxSiteAvailable) + ".vh-certbot.backup");
        std::error_code ec;
        std::filesystem::copy_file(siteAvailablePath, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return invalid("setup nginx: failed creating certbot backup of managed site: " + ec.message());

        std::string rendered;
        if (const auto renderError = renderManagedNginxConfig(runtimeConfig, certbotDomain, rendered))
            return invalid("setup nginx: " + *renderError);
        if (const auto writeErr = writeStringToFile(siteAvailablePath, rendered))
            return invalid("setup nginx: failed writing certbot-prepared managed nginx config: " + *writeErr);

        std::string prepReloadStatus;
        if (const auto prepErr = validateAndReloadNginx(prepReloadStatus)) {
            std::filesystem::copy_file(backupPath, siteAvailablePath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return invalid(
                    "setup nginx: certbot-prepared nginx config failed validation and backup restore also failed: " +
                    ec.message() + ". original failure: " + *prepErr);
            }

            std::string restoreReloadStatus;
            if (const auto restoreErr = validateAndReloadNginx(restoreReloadStatus); restoreErr) {
                return invalid(
                    "setup nginx: certbot-prepared nginx config failed validation ('" + *prepErr +
                    "') and backup restore validation also failed ('" + *restoreErr + "')");
            }

            return invalid(
                "setup nginx: certbot-prepared nginx config failed validation/reload; restored previous managed config");
        }

        const auto certbotIssue = runCapture(
            "certbot --nginx --non-interactive --agree-tos --register-unsafely-without-email "
            "--keep-until-expiring --domain " + shellQuote(certbotDomain));
        if (certbotIssue.code != 0) {
            std::filesystem::copy_file(backupPath, siteAvailablePath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return invalid(
                    "setup nginx: certbot issuance failed and backup restore also failed: " + ec.message() +
                    "\ncertbot failure: " + formatFailure("certbot --nginx --domain " + certbotDomain, certbotIssue));
            }

            std::string restoreReloadStatus;
            if (const auto restoreErr = validateAndReloadNginx(restoreReloadStatus); restoreErr) {
                return invalid(
                    "setup nginx: certbot issuance failed and backup restore validation/reload failed: " + *restoreErr +
                    "\ncertbot failure: " + formatFailure("certbot --nginx --domain " + certbotDomain, certbotIssue));
            }

            return invalid(
                "setup nginx: certbot issuance failed; restored previous managed nginx config safely\n"
                "certbot failure: " + formatFailure("certbot --nginx --domain " + certbotDomain, certbotIssue));
        }

        std::filesystem::remove(backupPath, ec);

        if (had80 && had443) {
            if (call.io) {
                call.io->print(
                    "setup nginx: detected managed config with both :80 and :443 before issuance; normalized to "
                    "managed HTTP-only certbot-preparation state before issuing certificate.\n");
            }
        }
    }

    if (const auto finalErr = validateAndReloadNginx(postCertReloadStatus)) {
        std::ostringstream err;
        err << "setup nginx: certbot flow completed, but final nginx validation/reload failed. "
            << "Configuration was left in place.\n";
        err << "failure: " << *finalErr;
        return invalid(err.str());
    }

    std::ostringstream out;
    out << "setup nginx: Vaulthalla nginx integration configured with certbot handling\n";
    out << "  site file: "
        << (createdSiteFile ? "installed" : rewroteSiteFile ? "regenerated from canonical config" : "already present")
        << " (" << kNginxSiteAvailable << ")\n";
    out << "  site link: " << (createdSiteLink ? "enabled" : "already enabled") << " (" << kNginxSiteEnabled << ")\n";
    out << "  config source: " << configPath.string() << "\n";
    if (skippedRewriteForExistingCert)
        out << "  managed config rewrite: skipped to preserve existing certbot-managed nginx HTTPS config\n";
    else
        out << "  managed config rewrite: applied canonical config generation\n";
    out << "  certbot domain: " << certbotDomain << "\n";
    out << "  certbot mode: " << certbotMode << "\n";
    out << "  certbot prerequisites: " << (prereqsInstalledNow ? "installed via apt-get during this run" : "already present") << "\n";
    out << "  reload: " << postCertReloadStatus;
    return ok(out.str());
}

static CommandResult handle_setup(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    if (isSetupMatch("assign-admin", sub)) return handle_setup_assign_admin(subcall);
    if (isSetupMatch("db", sub)) return handle_setup_db(subcall);
    if (isSetupMatch("remote-db", sub)) return handle_setup_remote_db(subcall);
    if (isSetupMatch("nginx", sub)) return handle_setup_nginx(subcall);

    return invalid(call.constructFullArgs(), "Unknown setup subcommand: '" + std::string(sub) + "'");
}

void commands::registerSetupCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("setup"), handle_setup);
}
