#include "usages.hpp"

using namespace vh::protocols::shell;

namespace vh::protocols::shell::secrets {

static std::shared_ptr<CommandUsage> buildBaseUsage(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = std::make_shared<CommandUsage>();
    cmd->parent = parent;
    return cmd;
}

static const auto secretPos = Positional::WithAliases("secret", "Name of the secret to manage (db-password)", {"db-password", "jwt-secret"});
static const auto fileOpt = Optional::Same("file", "Path to a file containing the secret value (default=/run/vaulthalla/<secret>) (deleted after reading)");
static const auto filePos = Positional::Alias("file", "Path to a file containing the secret value", "path");

static std::shared_ptr<CommandUsage> set(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"update", "set", "modify", "edit"};
    cmd->description = "Set or update an internal secret";
    cmd->positionals = { secretPos, filePos };
    cmd->examples = {
        {"vh secret set db-password --file /path/to/password.txt", "Set the database password from the specified file."},
        {"vh secret set jwt-secret --file /path/to/jwt_secret.txt", "Set the JWT secret from the specified file."},
        {"vh secret set db-password", "Set the database password from the default file location. (/run/vaulthalla/db-password)"}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> secret_export(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"export", "get", "download"};
    cmd->description = "Export an internal secret to a file";
    cmd->positionals = { secretPos };
    cmd->optional = { gpgRecipient, outputFile };
    cmd->examples = {
        {"vh secret export db-password /path/to/output_password.txt", "Export the database password to the specified file."},
        {"vh secret export jwt-secret /path/to/output_jwt_secret.txt", "Export the JWT secret to the specified file."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> base(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"secrets", "secret"};
    cmd->description = "Manage internal secrets used by Vaulthalla.";

    // --- subcommands (define pointers) ---
    const auto setCmd       = set(cmd->weak_from_this());
    const auto exportCmd    = secret_export(cmd->weak_from_this());

    const auto exportSingle = TestCommandUsage::Single(exportCmd);
    const auto setSingle    = TestCommandUsage::Single(setCmd);

    // --- examples ---
    cmd->examples = {
        {"vh secret set db-password --file /path/to/password.txt", "Set the database password from the specified file."},
        {"vh secret export jwt-secret --output /path/to/output_jwt_secret.txt", "Export the JWT secret to the specified file."}
    };

    // --- lifecycles ---
    setCmd->test_usage.lifecycle = { exportSingle };
    exportCmd->test_usage.setup = { setSingle };

    // --- finalize subcommands ---
    cmd->subcommands = {
        setCmd,
        exportCmd
    };

    return cmd;
}

std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent) {
    const auto book = std::make_shared<CommandBook>();
    book->title = "Secrets Commands";
    book->root = base(parent);
    return book;
}

}
