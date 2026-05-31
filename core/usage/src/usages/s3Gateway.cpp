#include "usages.hpp"

using namespace vh::protocols::shell;

namespace vh::protocols::shell::s3gateway {

namespace {
std::shared_ptr<CommandUsage> build(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = std::make_shared<CommandUsage>();
    cmd->parent = parent;
    return cmd;
}

std::shared_ptr<CommandUsage> status(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = build(parent);
    cmd->aliases = {"status"};
    cmd->description = "Show S3 gateway runtime status.";
    cmd->examples = {{"vh s3-gateway status", "Show endpoint, readiness, and request counters."}};
    return cmd;
}

std::shared_ptr<CommandUsage> enable(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = build(parent);
    cmd->aliases = {"enable"};
    cmd->description = "Enable the S3 gateway in config and restart its runtime service.";
    cmd->examples = {{"vh s3-gateway enable", "Enable the gateway listener using the configured endpoint."}};
    return cmd;
}

std::shared_ptr<CommandUsage> disable(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = build(parent);
    cmd->aliases = {"disable"};
    cmd->description = "Disable the S3 gateway in config and restart its runtime service.";
    cmd->examples = {{"vh s3-gateway disable", "Disable the gateway listener without affecting bucket bindings."}};
    return cmd;
}

std::shared_ptr<CommandUsage> creds(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = build(parent);
    cmd->aliases = {"creds", "credentials", "credential"};
    cmd->description = "Manage inbound S3 gateway credentials.";

    auto create = build(cmd->weak_from_this());
    create->aliases = {"create", "new", "add"};
    create->positionals = {Positional::Alias("name", "Credential name", "name")};
    create->optional = {Optional::ManyToOne("user", "User name or ID to own the credential", {"user", "u"}, "user")};
    create->optional_flags = {jsonFlag};
    create->examples = {{"vh s3-gateway creds create laptop --json", "Create an S3 access key and print the secret once."}};

    auto list = build(cmd->weak_from_this());
    list->aliases = {"list", "ls"};
    list->optional = {Optional::ManyToOne("user", "Filter by user name or ID", {"user", "u"}, "user")};
    list->optional_flags = {jsonFlag};
    list->examples = {{"vh s3-gateway creds list", "List gateway credentials visible to the caller."}};

    auto revoke = build(cmd->weak_from_this());
    revoke->aliases = {"revoke", "delete", "rm"};
    revoke->positionals = {Positional::Alias("access_key_or_name", "Access key ID or credential name", "access-key-or-name")};
    revoke->optional = {Optional::ManyToOne("user", "User name or ID when revoking by name", {"user", "u"}, "user")};
    revoke->examples = {{"vh s3-gateway creds revoke VH...", "Revoke a gateway credential."}};

    cmd->subcommands = {create, list, revoke};
    return cmd;
}

std::shared_ptr<CommandUsage> bucket(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = build(parent);
    cmd->aliases = {"bucket", "buckets"};
    cmd->description = "Manage S3 gateway bucket bindings.";

    auto list = build(cmd->weak_from_this());
    list->aliases = {"list", "ls"};
    list->optional_flags = {jsonFlag};
    list->examples = {{"vh s3-gateway bucket list", "List bound gateway buckets."}};

    auto bind = build(cmd->weak_from_this());
    bind->aliases = {"bind"};
    bind->positionals = {Positional::Alias("bucket", "Gateway bucket name", "bucket")};
    bind->required = {Option::Single("vault", "Vault ID or name to expose", "vault", "vault")};
    bind->optional = {
        Optional::ManyToOne("owner", "Vault owner when --vault is a name", {"owner"}, "user"),
        Optional::ManyToOne("mode", "Bucket mode: local, remote_cache, or remote_proxy", {"mode"}, "mode")
    };
    bind->optional_flags = {Flag::WithAliases("api_exclusive", "Mark binding API-exclusive", {"api-exclusive"})};
    bind->examples = {{"vh s3-gateway bucket bind photos --vault 12 --mode local", "Expose vault 12 as bucket photos."}};

    auto unbind = build(cmd->weak_from_this());
    unbind->aliases = {"unbind"};
    unbind->positionals = {Positional::Alias("bucket", "Gateway bucket name", "bucket")};
    unbind->examples = {{"vh s3-gateway bucket unbind photos", "Remove a gateway bucket binding."}};

    auto createLocal = build(cmd->weak_from_this());
    createLocal->aliases = {"create-local"};
    createLocal->positionals = {Positional::Alias("bucket", "Gateway bucket name", "bucket")};
    createLocal->optional = {
        Optional::ManyToOne("owner", "Owner name or ID", {"owner"}, "user"),
        Optional::ManyToOne("quota", "Vault quota size", {"quota"}, "size")
    };
    createLocal->examples = {{"vh s3-gateway bucket create-local archive --quota 500G", "Create and bind a local encrypted bucket."}};

    auto createRemoteCache = build(cmd->weak_from_this());
    createRemoteCache->aliases = {"create-remote-cache"};
    createRemoteCache->positionals = {Positional::Alias("bucket", "Gateway bucket name", "bucket")};
    createRemoteCache->required = {
        Option::Single("api_key", "Existing upstream S3/R2 API key name or ID", "api-key", "api-key"),
        Option::Single("upstream_bucket", "Upstream S3/R2 bucket name", "upstream-bucket", "bucket")
    };
    createRemoteCache->optional_flags = {
        Flag::WithAliases("encrypt", "Encrypt objects before upstream upload", {"encrypt"}),
        Flag::WithAliases("no_encrypt", "Do not encrypt objects before upstream upload", {"no-encrypt"})
    };
    createRemoteCache->examples = {
        {"vh s3-gateway bucket create-remote-cache edge --api-key r2-main --upstream-bucket origin --encrypt",
         "Create an API-exclusive smart-cache gateway bucket backed by an upstream S3/R2 bucket."}
    };

    cmd->subcommands = {list, bind, unbind, createLocal, createRemoteCache};
    return cmd;
}
} // namespace

std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent) {
    const auto book = std::make_shared<CommandBook>();
    book->title = "S3 Gateway Commands";
    auto root = build(parent);
    root->aliases = {"s3-gateway", "s3gw"};
    root->description = "Manage the Vaulthalla S3-compatible gateway.";
    root->subcommands = {status(root->weak_from_this()), enable(root->weak_from_this()), disable(root->weak_from_this()),
                         creds(root->weak_from_this()), bucket(root->weak_from_this())};
    root->examples = {
        {"vh s3-gateway status", "Show gateway runtime status."},
        {"vh s3-gateway creds create laptop", "Create inbound S3 credentials."},
        {"vh s3-gateway bucket bind photos --vault 12", "Expose an existing vault as a bucket."}
    };
    book->root = root;
    return book;
}

} // namespace vh::protocols::shell::s3gateway
