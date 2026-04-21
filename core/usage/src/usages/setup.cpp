#include "usages.hpp"

using namespace vh::protocols::shell;

namespace vh::protocols::shell::setup {

static std::shared_ptr<CommandUsage> buildBaseUsage(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = std::make_shared<CommandUsage>();
    cmd->parent = parent;
    return cmd;
}

static std::shared_ptr<CommandUsage> db(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"db", "database", "postgres"};
    cmd->description = "Bootstrap Vaulthalla local PostgreSQL integration (role/database) and hand off schema/migrations to normal runtime startup.";
    cmd->examples = {
        {"vh setup db", "Create/reuse local PostgreSQL role/database, seed runtime DB password handoff when needed, and restart/start runtime service."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> nginx(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"nginx", "proxy"};
    cmd->description = "Install and enable Vaulthalla nginx site integration conservatively.";
    cmd->examples = {
        {"vh setup nginx", "Install/enable Vaulthalla nginx site config and reload nginx when safe."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> base(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"setup"};
    cmd->description = "Perform explicit Vaulthalla integration setup tasks.";
    cmd->examples = {
        {"vh setup db", "Bootstrap local PostgreSQL integration."},
        {"vh setup nginx", "Configure Vaulthalla nginx integration."}
    };
    cmd->subcommands = {
        db(cmd->weak_from_this()),
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
