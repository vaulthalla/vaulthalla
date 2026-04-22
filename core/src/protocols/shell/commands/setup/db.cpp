#include "protocols/shell/commands/setup.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/commandHelpers.hpp"
#include "config/Config.hpp"
#include "db/query/identities/User.hpp"
#include "CommandUsage.hpp"

#include <paths.h>

#include <algorithm>
#include <cstdint>
#include <exception>
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
#include <unistd.h>
#include <vector>

namespace vh::protocols::shell::commands::setup {

namespace setup_db_util = vh::protocols::shell::util;

namespace {

constexpr auto* kDbUser = "vaulthalla";
constexpr auto* kDbName = "vaulthalla";
constexpr auto* kServiceUnit = "vaulthalla.service";
constexpr auto* kPendingDbPasswordFile = "/run/vaulthalla/db_password";

struct ServiceIdentity {
    uid_t uid = 0;
    gid_t gid = 0;
};

std::string sqlLiteral(const std::string& s) {
    std::string out{"'"};
    out.reserve(s.size() + 2);
    for (const char c : s) {
        if (c == '\'') out += "''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

ExecResult psqlSql(const std::string& prefix, const std::string& db, const std::string& sql) {
    return setup_db_util::runCapture(prefix + " -d " + setup_db_util::shellQuote(db) + " -tAc " + setup_db_util::shellQuote(sql));
}

std::vector<std::filesystem::path> listSqlFilesSorted(const std::filesystem::path& dir) {
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

std::string makeDbPassword(const size_t len = 48) {
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

std::optional<std::string> resolveServiceIdentity(ServiceIdentity& identity) {
    const auto* user = ::getpwnam(kDbUser);
    if (!user) return "system user '" + std::string(kDbUser) + "' not found";

    identity.uid = user->pw_uid;
    identity.gid = user->pw_gid;

    if (const auto* group = ::getgrnam(kDbUser); group)
        identity.gid = group->gr_gid;

    return std::nullopt;
}

std::optional<std::string> enforceDbPasswordFileState(const std::filesystem::path& pendingPath, const ServiceIdentity& identity) {
    struct stat st{};
    if (::stat(pendingPath.c_str(), &st) != 0)
        return "failed to stat pending password file: " + pendingPath.string();

    if ((st.st_uid != identity.uid || st.st_gid != identity.gid) &&
        ::chown(pendingPath.c_str(), identity.uid, identity.gid) != 0) {
        const auto sudoChown = setup_db_util::runCapture(
            "sudo -n chown " + std::string(kDbUser) + ":" + std::string(kDbUser) + " " +
            setup_db_util::shellQuote(pendingPath.string()));
        if (sudoChown.code != 0)
            return "failed setting ownership to " + std::string(kDbUser) + ":" + std::string(kDbUser) + " on " + pendingPath.string();
    }

    if ((st.st_mode & 0777) != (S_IRUSR | S_IWUSR) && ::chmod(pendingPath.c_str(), S_IRUSR | S_IWUSR) != 0) {
        const auto sudoChmod = setup_db_util::runCapture("sudo -n chmod 0600 " + setup_db_util::shellQuote(pendingPath.string()));
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

std::optional<std::string> writePendingDbPassword(const std::string& pass) {
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

std::optional<std::string> choosePostgresPrefix() {
    std::vector<std::string> prefixes;

    if (setup_db_util::hasEffectiveRoot() && setup_db_util::commandExists("runuser"))
        prefixes.emplace_back("runuser -u postgres -- psql -X -v ON_ERROR_STOP=1");

    if (setup_db_util::canUsePasswordlessSudo())
        prefixes.emplace_back("sudo -n -u postgres psql -X -v ON_ERROR_STOP=1");

    for (const auto& prefix : prefixes) {
        const auto probe = setup_db_util::runCapture(prefix + " -d postgres -tAc " + setup_db_util::shellQuote("SELECT 1;"));
        if (probe.code == 0 && setup_db_util::trim(probe.output) == "1") return prefix;
    }

    return std::nullopt;
}

std::optional<std::string> restartOrStartService(std::string& actionTaken) {
    if (!setup_db_util::commandExists("systemctl"))
        return "systemctl is not available; cannot hand off DB bootstrap to runtime startup";

    const auto prefix = setup_db_util::privilegedPrefix();
    if (prefix.empty())
        return "insufficient privileges for systemctl; run with root or passwordless sudo access";

    const auto active = setup_db_util::runCapture(prefix + "systemctl --quiet is-active " + kServiceUnit);
    const bool isActive = active.code == 0;

    ExecResult serviceAction;
    if (isActive) {
        serviceAction = setup_db_util::runCapture(prefix + "systemctl restart " + kServiceUnit);
        actionTaken = "restarted";
    } else {
        serviceAction = setup_db_util::runCapture(prefix + "systemctl start " + kServiceUnit);
        actionTaken = "started";
    }

    if (serviceAction.code != 0)
        return setup_db_util::formatFailure("failed to " + actionTaken + " " + kServiceUnit, serviceAction);

    return std::nullopt;
}

std::optional<std::string> requiredOptionValue(const CommandCall& call,
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

std::optional<std::string> optionalOptionValue(const CommandCall& call,
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

std::optional<std::string> parsePortValue(const std::string& raw, uint16_t& port) {
    const auto parsed = parseUInt(raw);
    if (!parsed || *parsed == 0 || *parsed > 65535)
        return "invalid port '" + raw + "' (expected integer 1-65535)";
    port = static_cast<uint16_t>(*parsed);
    return std::nullopt;
}

std::optional<std::string> parsePoolSizeValue(const std::optional<std::string>& raw, int& poolSize) {
    if (!raw || raw->empty()) return std::nullopt;
    const auto parsed = parseUInt(*raw);
    if (!parsed || *parsed == 0)
        return "invalid pool size '" + *raw + "' (expected positive integer)";
    poolSize = static_cast<int>(*parsed);
    return std::nullopt;
}

std::optional<std::string> loadPasswordFromFile(const std::string& filePath, std::string& password) {
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

}

CommandResult handleAssignAdmin(const CommandCall& call) {
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

CommandResult handleDb(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "db"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("setup db: requires super_admin role");

    if (!setup_db_util::hasRootOrEquivalentPrivileges()) {
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

    if (!setup_db_util::commandExists("psql"))
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
    if (roleState.code != 0) return invalid("setup db: " + setup_db_util::formatFailure("failed querying PostgreSQL role state", roleState));
    const bool roleExists = setup_db_util::trim(roleState.output) == "1";

    const auto dbState = psqlSql(
        *postgresPrefix, "postgres", "SELECT 1 FROM pg_database WHERE datname = " + sqlLiteral(kDbName) + ";");
    if (dbState.code != 0) return invalid("setup db: " + setup_db_util::formatFailure("failed querying PostgreSQL database state", dbState));
    const bool dbExists = setup_db_util::trim(dbState.output) == "1";

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
            return invalid("setup db: " + setup_db_util::formatFailure("failed creating PostgreSQL role '" + std::string(kDbUser) + "'", createRole));
        createdRole = true;
    }

    if (!dbExists) {
        const auto createDb = psqlSql(
            *postgresPrefix, "postgres", "CREATE DATABASE " + std::string(kDbName) + " OWNER " + std::string(kDbUser) + ";");
        if (createDb.code != 0)
            return invalid("setup db: " + setup_db_util::formatFailure("failed creating PostgreSQL database '" + std::string(kDbName) + "'", createDb));
        createdDb = true;
    }

    const auto grantDb = psqlSql(
        *postgresPrefix, "postgres", "GRANT ALL PRIVILEGES ON DATABASE " + std::string(kDbName) + " TO " + std::string(kDbUser) + ";");
    if (grantDb.code != 0) return invalid("setup db: " + setup_db_util::formatFailure("failed granting database privileges", grantDb));

    const auto grantSchema = psqlSql(
        *postgresPrefix, kDbName, "GRANT USAGE, CREATE ON SCHEMA public TO " + std::string(kDbUser) + ";");
    if (grantSchema.code != 0) return invalid("setup db: " + setup_db_util::formatFailure("failed granting schema privileges", grantSchema));

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

CommandResult handleRemoteDb(const CommandCall& call) {
    const auto usage = resolveUsage({"setup", "remote-db"});
    validatePositionals(call, usage);

    if (!call.user->isSuperAdmin())
        return invalid("setup remote-db: requires super_admin role");

    if (!setup_db_util::hasEffectiveRoot())
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

}
