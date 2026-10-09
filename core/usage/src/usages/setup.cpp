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
static const auto nginxS3DomainOpt = Optional::ManyToOne("s3_domain", "Dedicated HTTPS hostname for the S3 gateway (must differ from --domain). Requires --certbot-dns-cloudflare <credentials>; --certbot cannot be combined with it", {"s3-domain"}, "domain");
static const auto nginxCertbotDnsCloudflareOpt = Optional::ManyToOne("certbot_dns_cloudflare", "Issue/renew one certificate covering --domain and --s3-domain through Cloudflare DNS-01 (python3-certbot-dns-cloudflare); no inbound port 80 is needed. <credentials> is a root-owned, mode 0600 file containing the line 'dns_cloudflare_api_token = <token>', where the token has Zone > DNS > Edit on the zones of both hostnames. Certbot renewals keep reading the file, so leave it in place. Conventional path: /etc/vaulthalla/certbot/cloudflare.ini", {"certbot-dns-cloudflare"}, "credentials");
static const auto nginxCertbotFlag = Flag::WithAliases("certbot_mode", "Issue/renew a Let's Encrypt certificate for --domain with the certbot nginx plugin (HTTP-01: the domain must reach this host on port 80). Not supported with --s3-domain", {"certbot"});

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
    cmd->description = "Generate/deploy Vaulthalla-managed nginx config from canonical runtime config, then validate and reload conservatively. Requires sudo. "
                       "TLS: --certbot issues a certificate for --domain over HTTP-01. --s3-domain needs --certbot-dns-cloudflare <credentials> instead, "
                       "which issues one certificate for both hostnames over Cloudflare DNS-01. Create the credentials file once with "
                       "'sudo install -d -m 0700 /etc/vaulthalla/certbot && sudo install -m 0600 /dev/null /etc/vaulthalla/certbot/cloudflare.ini "
                       "&& sudoedit /etc/vaulthalla/certbot/cloudflare.ini' and add the line 'dns_cloudflare_api_token = <token>' "
                       "(Cloudflare dashboard: My Profile > API Tokens > 'Edit zone DNS' template). Guide: https://vaulthalla.io/docs/s3-gateway/setup";
    cmd->optional = {nginxDomainOpt, nginxS3DomainOpt, nginxCertbotDnsCloudflareOpt};
    cmd->optional_flags = {nginxCertbotFlag};
    cmd->examples = {
        {"sudo vh setup nginx", "Regenerate and apply canonical Vaulthalla-managed nginx config, then validate/reload when safe."},
        {"sudo vh setup nginx --certbot --domain vault.example.com", "Configure Vaulthalla nginx integration and run deterministic certbot issue/renew flow for the domain."},
        {"sudo vh setup nginx --domain vaulthalla.dev --s3-domain s3.vaulthalla.dev --certbot-dns-cloudflare /etc/vaulthalla/certbot/cloudflare.ini",
         "Configure HTTPS web and dedicated S3 hosts using Cloudflare DNS-01 certificates."}
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

static std::shared_ptr<CommandUsage> set_super_admin_password(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"set-super-admin-password", "super-admin-password"};
    cmd->description = "Change the web console password of the built-in super-admin account ('admin'). Asks for the new "
                       "password. Only the Linux user bound as the Vaulthalla super admin (see 'vh setup assign-admin') may "
                       "run it; do not use sudo. Ends the account's web sessions and removes the initial password file "
                       "(/var/lib/vaulthalla/super_admin_initial_password) if it is still there. Keeping the generated "
                       "initial password instead is fine: delete that file.";
    cmd->examples = {
        {"vh setup set-super-admin-password", "Prompt for and set a new web console password for 'admin'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> base(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"setup"};
    cmd->description = "Perform explicit Vaulthalla integration setup tasks.";
    cmd->examples = {
        {"vh setup assign-admin", "Run explicit admin-claim onboarding for the current operator."},
        {"vh setup set-super-admin-password", "Change the web console password of the built-in 'admin' account."},
        {"sudo vh setup db", "Bootstrap local PostgreSQL integration."},
        {"sudo vh setup remote-db --host db.example.net --user vaulthalla --database vaulthalla --password-file /path/to/password-file", "Configure remote PostgreSQL integration."},
        {"sudo vh setup nginx", "Configure Vaulthalla nginx integration."},
        {"sudo vh setup nginx --certbot --domain vault.example.com", "Configure Vaulthalla nginx integration with explicit certbot handling for the requested domain."},
        {"sudo vh setup nginx --domain vaulthalla.dev --s3-domain s3.vaulthalla.dev --certbot-dns-cloudflare /etc/vaulthalla/certbot/cloudflare.ini",
         "Configure HTTPS web and dedicated S3 hosts using Cloudflare DNS-01 certificates."}
    };
    cmd->subcommands = {
        assign_admin(cmd->weak_from_this()),
        db(cmd->weak_from_this()),
        remote_db(cmd->weak_from_this()),
        nginx(cmd->weak_from_this()),
        set_super_admin_password(cmd->weak_from_this())
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
