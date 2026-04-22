#include "CommandUsage.hpp"
#include "config/Config.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/commands/router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/commandHelpers.hpp"
#include "protocols/shell/util/nginxHelpers.hpp"
#include "identities/User.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <paths.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace vh::protocols::shell::commands::setup {

namespace setup_nginx_cmd_util = vh::protocols::shell::util;
namespace setup_nginx_util = vh::protocols::shell::util;

namespace {

constexpr auto* kNginxTemplate = "/usr/share/vaulthalla/nginx/vaulthalla.conf";
constexpr auto* kWebUpstreamHost = "127.0.0.1";
constexpr uint16_t kWebUpstreamPort = 36968;

struct CertbotPrereqState {
    bool certbot = false;
    bool nginxPlugin = false;
};

bool hasNonNginxListenersOnWebPorts() {
    if (!setup_nginx_cmd_util::commandExists("ss")) return false;

    const auto listeners = setup_nginx_cmd_util::runCapture("ss -H -ltnp '( sport = :80 or sport = :443 )'");
    if (listeners.code != 0 || listeners.output.empty()) return false;

    std::istringstream in(listeners.output);
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = setup_nginx_cmd_util::trim(line);
        if (trimmed.empty()) continue;
        if (trimmed.find("nginx") == std::string::npos) return true;
    }
    return false;
}

bool hasCustomNginxSitesEnabled() {
    const std::filesystem::path sitesEnabled{"/etc/nginx/sites-enabled"};
    if (!std::filesystem::exists(sitesEnabled) || !std::filesystem::is_directory(sitesEnabled)) return false;

    return std::ranges::any_of(std::filesystem::directory_iterator(sitesEnabled), [](const auto& entry) {
        const auto base = entry.path().filename().string();
        return !base.empty() && base != "default" && base != "vaulthalla";
    });
}

bool filesEqual(const std::filesystem::path& a, const std::filesystem::path& b) {
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

bool tokenizedLineContainsDomain(const std::string& line, const std::string& domain) {
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

bool certbotCertificatesMentionDomain(const std::string& output, const std::string& domain) {
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = setup_nginx_cmd_util::trim(line);
        if (trimmed.rfind("Domains:", 0) != 0) continue;
        const auto domainsPart = setup_nginx_cmd_util::trim(trimmed.substr(std::string("Domains:").size()));
        if (tokenizedLineContainsDomain(domainsPart, domain))
            return true;
    }
    return false;
}

bool hasCertbotManagedCertForDomain(const std::string& domain) {
    if (const std::filesystem::path livePath = std::filesystem::path("/etc/letsencrypt/live") / domain;
        std::filesystem::exists(livePath / "fullchain.pem")
        && std::filesystem::exists(livePath / "privkey.pem"))
        return true;

    if (!setup_nginx_cmd_util::commandExists("certbot")) return false;
    const auto [code, output] = setup_nginx_cmd_util::runCapture("certbot certificates");
    if (code != 0) return false;
    return certbotCertificatesMentionDomain(output, domain);
}

bool isLikelyDomain(const std::string& raw) {
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

bool containsListen80(const std::string& content) {
    return content.find("listen 80") != std::string::npos ||
           content.find("listen [::]:80") != std::string::npos;
}

bool containsListen443(const std::string& content) {
    return content.find("listen 443") != std::string::npos ||
           content.find("listen [::]:443") != std::string::npos;
}

bool looksCertbotManagedNginxConfig(const std::string& content) {
    return content.find("managed by Certbot") != std::string::npos ||
           content.find("/etc/letsencrypt/live/") != std::string::npos ||
           content.find("ssl_certificate ") != std::string::npos;
}

std::optional<std::string> readFileToString(const std::filesystem::path& path, std::string& content) {
    std::ifstream in(path);
    if (!in.is_open()) return "failed opening file: " + path.string();
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return std::nullopt;
}

std::optional<std::string> writeStringToFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return "failed opening file for write: " + path.string();
    out << content;
    if (!out.good()) return "failed writing file: " + path.string();
    return std::nullopt;
}

std::string normalizedLocalUpstreamHost(const std::string& hostRaw) {
    auto host = setup_nginx_cmd_util::trim(hostRaw);
    if (host.empty() || host == "0.0.0.0" || host == "::" || host == "[::]")
        return "127.0.0.1";
    return host;
}

std::string hostForProxyPass(const std::string& hostRaw) {
    auto host = normalizedLocalUpstreamHost(hostRaw);
    if (host.find(':') != std::string::npos && !host.starts_with('['))
        return "[" + host + "]";
    return host;
}

std::optional<std::string> renderManagedNginxConfig(const config::Config& cfg,
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

CertbotPrereqState detectCertbotPrereqs() {
    CertbotPrereqState out;
    out.certbot = setup_nginx_cmd_util::commandExists("certbot");
    if (!out.certbot) return out;

    const auto plugins = setup_nginx_cmd_util::runCapture("certbot plugins");
    out.nginxPlugin = plugins.code == 0 && plugins.output.find("nginx") != std::string::npos;
    return out;
}

std::optional<std::string> ensureCertbotPrereqs(const CommandCall& call, bool& installedNow) {
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

    const bool aptSupported = std::filesystem::exists("/etc/debian_version") && setup_nginx_cmd_util::commandExists("apt-get");
    if (!aptSupported) {
        return "automatic certbot prerequisite install is only supported on Debian/Ubuntu apt environments. "
               "Install packages manually and rerun.";
    }

    const auto prefix = setup_nginx_cmd_util::privilegedPrefix();
    if (prefix.empty())
        return "insufficient privileges to install certbot prerequisites; rerun as root or with passwordless sudo";

    const auto install = setup_nginx_cmd_util::runCapture(prefix + "DEBIAN_FRONTEND=noninteractive apt-get install -y certbot python3-certbot-nginx");
    if (install.code != 0)
        return setup_nginx_cmd_util::formatFailure("failed installing certbot prerequisites", install);

    prereqs = detectCertbotPrereqs();
    if (!prereqs.certbot || !prereqs.nginxPlugin)
        return "certbot prerequisites still unavailable after install attempt";

    installedNow = true;
    return std::nullopt;
}

std::optional<std::string> validateAndReloadNginx(std::string& reloadStatus) {
    const auto nginxTest = setup_nginx_cmd_util::runCapture("nginx -t");
    if (nginxTest.code != 0) {
        if (!nginxTest.output.empty())
            return "nginx -t failed: " + nginxTest.output;
        return "nginx -t failed";
    }

    reloadStatus = "not attempted (systemctl unavailable or nginx inactive)";
    if (setup_nginx_cmd_util::commandExists("systemctl") &&
        setup_nginx_cmd_util::runCapture("systemctl --quiet is-active nginx.service").code == 0) {
        const auto reload = setup_nginx_cmd_util::runCapture("systemctl reload nginx.service");
        if (reload.code != 0)
            return setup_nginx_cmd_util::formatFailure("systemctl reload nginx.service", reload);
        reloadStatus = "nginx reloaded";
    }

    return std::nullopt;
}

}

CommandResult handleNginx(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "nginx"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("setup nginx: requires super_admin role");

    if (!setup_nginx_cmd_util::hasEffectiveRoot())
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
        certbotDomain = setup_nginx_cmd_util::trim(*domainOpt);
        if (!isLikelyDomain(certbotDomain))
            return invalid("setup nginx: invalid domain '" + certbotDomain + "'");
    }

    if (!setup_nginx_cmd_util::commandExists("nginx") || !std::filesystem::exists("/etc/nginx"))
        return invalid("setup nginx: nginx is not installed or /etc/nginx is missing");

    if (hasNonNginxListenersOnWebPorts())
        return invalid("setup nginx: detected non-nginx listeners on :80/:443; refusing automatic integration");

    if (hasCustomNginxSitesEnabled() && !std::filesystem::exists(setup_nginx_util::kNginxSiteEnabled))
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

    const std::filesystem::path siteSetupBackupPath = std::filesystem::path(std::string(setup_nginx_util::kNginxSiteAvailable) + ".vh-setup.backup");

    try {
        std::filesystem::create_directories("/etc/nginx/sites-available");
        std::filesystem::create_directories("/etc/nginx/sites-enabled");

        const std::filesystem::path templatePath{kNginxTemplate};
        const std::filesystem::path siteAvailPath{setup_nginx_util::kNginxSiteAvailable};
        const std::filesystem::path siteEnabledPath{setup_nginx_util::kNginxSiteEnabled};
        const std::filesystem::path markerPath{setup_nginx_util::kNginxManagedMarker};

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

        if (std::filesystem::is_symlink(siteEnabledPath) && !setup_nginx_util::isManagedSiteSymlinkTarget(siteEnabledPath))
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
            std::filesystem::remove(setup_nginx_util::kNginxSiteEnabled, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }
        if (createdSiteFile) {
            std::filesystem::remove(setup_nginx_util::kNginxSiteAvailable, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        } else if (backedUpSiteFile) {
            std::filesystem::copy_file(siteSetupBackupPath, setup_nginx_util::kNginxSiteAvailable, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) rollbackOk = false;
            ec.clear();
        }
        if (createdMarker) {
            std::filesystem::remove(setup_nginx_util::kNginxManagedMarker, ec);
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
                << "  - " << setup_nginx_util::kNginxSiteEnabled << "\n"
                << "  - " << setup_nginx_util::kNginxSiteAvailable << "\n"
                << "  - " << setup_nginx_util::kNginxManagedMarker;
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
            << " (" << setup_nginx_util::kNginxSiteAvailable << ")\n";
        out << "  site link: " << (createdSiteLink ? "enabled" : "already enabled") << " (" << setup_nginx_util::kNginxSiteEnabled << ")\n";
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

    if (!std::filesystem::exists(setup_nginx_util::kNginxManagedMarker))
        return invalid("setup nginx: certbot mode requires Vaulthalla-managed nginx marker state");

    bool prereqsInstalledNow = false;
    if (const auto prereqError = ensureCertbotPrereqs(call, prereqsInstalledNow))
        return invalid("setup nginx: " + *prereqError);

    const std::filesystem::path siteAvailablePath{setup_nginx_util::kNginxSiteAvailable};
    std::string certbotMode;
    std::string postCertReloadStatus = reloadStatus;

    if (hasExistingCert) {
        certbotMode = "existing certificate detected (renew-safe path)";
        const auto renew = setup_nginx_cmd_util::runCapture("certbot renew --cert-name " + setup_nginx_cmd_util::shellQuote(certbotDomain) + " --non-interactive");
        if (renew.code != 0) {
            std::ostringstream err;
            err << "setup nginx: existing certificate state detected for " << certbotDomain << ", "
                << "but certbot renew failed\n"
                << "renew failure: " << setup_nginx_cmd_util::formatFailure("certbot renew --cert-name " + certbotDomain, renew);
            return invalid(err.str());
        }
    } else {
        certbotMode = "no existing certificate detected (fresh issuance path)";

        std::string currentContent;
        if (const auto readErr = readFileToString(siteAvailablePath, currentContent))
            return invalid("setup nginx: failed reading managed site before certbot issuance: " + *readErr);

        const bool had80 = containsListen80(currentContent);
        const bool had443 = containsListen443(currentContent);

        const std::filesystem::path backupPath = std::filesystem::path(std::string(setup_nginx_util::kNginxSiteAvailable) + ".vh-certbot.backup");
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

        const auto certbotIssue = setup_nginx_cmd_util::runCapture(
            "certbot --nginx --non-interactive --agree-tos --register-unsafely-without-email "
            "--keep-until-expiring --domain " + setup_nginx_cmd_util::shellQuote(certbotDomain));
        if (certbotIssue.code != 0) {
            std::filesystem::copy_file(backupPath, siteAvailablePath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return invalid(
                    "setup nginx: certbot issuance failed and backup restore also failed: " + ec.message() +
                    "\ncertbot failure: " + setup_nginx_cmd_util::formatFailure("certbot --nginx --domain " + certbotDomain, certbotIssue));
            }

            std::string restoreReloadStatus;
            if (const auto restoreErr = validateAndReloadNginx(restoreReloadStatus); restoreErr) {
                return invalid(
                    "setup nginx: certbot issuance failed and backup restore validation/reload failed: " + *restoreErr +
                    "\ncertbot failure: " + setup_nginx_cmd_util::formatFailure("certbot --nginx --domain " + certbotDomain, certbotIssue));
            }

            return invalid(
                "setup nginx: certbot issuance failed; restored previous managed nginx config safely\n"
                "certbot failure: " + setup_nginx_cmd_util::formatFailure("certbot --nginx --domain " + certbotDomain, certbotIssue));
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
        << " (" << setup_nginx_util::kNginxSiteAvailable << ")\n";
    out << "  site link: " << (createdSiteLink ? "enabled" : "already enabled") << " (" << setup_nginx_util::kNginxSiteEnabled << ")\n";
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

}
