#include "usages.hpp"

using namespace vh::protocols::shell;

namespace vh::protocols::shell::setup {

static std::shared_ptr<CommandUsage> buildBaseUsage(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = std::make_shared<CommandUsage>();
    cmd->parent = parent;
    return cmd;
}

static const auto dbHostReq = Option::Single("host", "Remote PostgreSQL host", "host", "hostname");
static const auto dbPortOpt = Optional::ManyToOne("port", "Remote PostgreSQL port", {"port", "p"}, "port", "5432");
static const auto dbUserReq = Option::Multi("user", "Remote PostgreSQL user", {"user", "u"}, {"username"});
static const auto dbNameReq = Option::Multi("database", "Remote PostgreSQL database name", {"database", "db", "name"}, {"database"});
static const auto dbPassFileReq = Option::Multi("password_file", "Path to file containing remote DB password", {"password-file", "password-path", "pw-file"}, {"path"});
static const auto dbPoolSizeOpt = Optional::ManyToOne("pool_size", "Database pool size", {"pool-size"}, "size");
static const auto interactiveFlag = Flag::WithAliases("interactive_mode", "Prompt for missing remote DB values", {"interactive", "i"});
static const auto nginxDomainOpt = Optional::ManyToOne("domain", "Domain to configure for Vaulthalla nginx integration", {"domain", "d"}, "domain");
static const auto nginxCertbotFlag = Flag::WithAliases("certbot_mode", "Use certbot nginx integration for certificate issue/renew handling", {"certbot"});

static std::shared_ptr<CommandUsage> db(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"db", "database", "postgres"};
    cmd->description = "Bootstrap Vaulthalla local PostgreSQL integration (role/database) and hand off schema/migrations to normal runtime startup. Requires sudo.";
    cmd->examples = {
        {"sudo vh setup db", "Create/reuse local PostgreSQL role/database, seed runtime DB password handoff when needed, and restart/start runtime service."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> remote_db(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"remote-db", "remote_db", "remote", "rdb"};
    cmd->description = "Configure Vaulthalla to use a remote PostgreSQL database. Requires sudo.";
    cmd->required = {dbHostReq, dbUserReq, dbNameReq, dbPassFileReq};
    cmd->optional = {dbPortOpt, dbPoolSizeOpt};
    cmd->optional_flags = {interactiveFlag};
    cmd->examples = {
        {"sudo vh setup remote-db --host db.example.net --port 5432 --user vaulthalla --database vaulthalla --password-file /root/.secrets/vh-db-pass",
         "Configure remote DB settings, seed runtime DB password handoff, and restart/start service."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> nginx(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"nginx", "proxy"};
    cmd->description = "Generate/deploy Vaulthalla-managed nginx config from canonical runtime config, then validate and reload conservatively. Requires sudo.";
    cmd->optional = {nginxDomainOpt};
    cmd->optional_flags = {nginxCertbotFlag};
    cmd->examples = {
        {"sudo vh setup nginx", "Regenerate and apply canonical Vaulthalla-managed nginx config, then validate/reload when safe."},
        {"sudo vh setup nginx --certbot --domain vault.example.com", "Configure Vaulthalla nginx integration and run deterministic certbot issue/renew flow for the domain."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> assign_admin(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"assign-admin", "assign_admin", "claim-admin"};
    cmd->description = "Explicitly claim or verify initial CLI super-admin ownership for the current operator.";
    cmd->examples = {
        {"vh setup assign-admin", "Run the explicit admin-claim onboarding step and report whether ownership was newly bound or already configured."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> base(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"setup"};
    cmd->description = "Perform explicit Vaulthalla integration setup tasks.";
    cmd->examples = {
        {"vh setup assign-admin", "Run explicit admin-claim onboarding for the current operator."},
        {"sudo vh setup db", "Bootstrap local PostgreSQL integration."},
        {"sudo vh setup remote-db --host db.example.net --user vaulthalla --database vaulthalla --password-file /path/to/password-file", "Configure remote PostgreSQL integration."},
        {"sudo vh setup nginx", "Configure Vaulthalla nginx integration."},
        {"sudo vh setup nginx --certbot --domain vault.example.com", "Configure Vaulthalla nginx integration with explicit certbot handling for the requested domain."}
    };
    cmd->subcommands = {
        assign_admin(cmd->weak_from_this()),
        db(cmd->weak_from_this()),
        remote_db(cmd->weak_from_this()),
        nginx(cmd->weak_from_this())
    };
    return cmd;
}

std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent) {
    const auto book = std::make_shared<CommandBook>();
    book->title = "Setup Commands";
    book->root = base(parent);
    return book;
}

}
