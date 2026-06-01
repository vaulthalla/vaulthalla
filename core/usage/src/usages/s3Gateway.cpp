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
    create->optional = {
        Optional::ManyToOne("user", "Principal user name or ID", {"user", "u"}, "user"),
        Optional::ManyToOne("scope", "Scope: user-access, global, or vault-allowlist", {"scope"}, "scope"),
        Optional::ManyToOne("vault", "Vault ID or name allowed by the credential; repeat for multiple vaults", {"vault"}, "vault"),
        Optional::ManyToOne("expires", "Credential lifetime, such as 30d or 12h", {"expires"}, "duration"),
        Optional::ManyToOne("description", "Credential description", {"description"}, "text")
    };
    create->optional_flags = {
        Flag::WithAliases("list", "Allow List operations for scoped vaults", {"list"}),
        Flag::WithAliases("read", "Allow Read operations for scoped vaults", {"read"}),
        Flag::WithAliases("write", "Allow Write operations for scoped vaults", {"write"}),
        Flag::WithAliases("delete", "Allow Delete operations for scoped vaults", {"delete"}),
        Flag::WithAliases("admin", "Allow bucket admin operations for scoped vaults", {"admin"}),
        Flag::WithAliases("enforce-budget-for-local-requests", "Count local/cache hits against gateway key budgets", {"enforce-budget-for-local-requests"}),
        jsonFlag
    };
    create->examples = {
        {"vh s3-gateway creds create laptop --json", "Create a user-access key and print the secret once."},
        {"vh s3-gateway creds create laptop --enforce-budget-for-local-requests", "Create a key that counts local/cache hits against gateway budgets."},
        {"vh s3-gateway creds create backup --scope vault-allowlist --vault photos --read --write --json",
         "Create a key restricted to one vault."}
    };

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

    auto scope = build(cmd->weak_from_this());
    scope->aliases = {"scope"};
    scope->positionals = {Positional::Alias("access_key_or_name", "Access key ID or credential name", "access-key-or-name")};
    scope->description = "View or change S3 gateway credential vault scope.";

    auto scopeShow = build(scope->weak_from_this());
    scopeShow->aliases = {"show"};
    scopeShow->optional_flags = {jsonFlag};
    scopeShow->examples = {{"vh s3-gateway creds scope backup show", "Show scope rows for a credential."}};

    auto scopeSet = build(scope->weak_from_this());
    scopeSet->aliases = {"set"};
    scopeSet->optional = {
        Optional::ManyToOne("scope", "Scope: user-access, global, or vault-allowlist", {"scope"}, "scope"),
        Optional::ManyToOne("user", "Retarget principal user name or ID", {"user", "u"}, "user"),
        Optional::ManyToOne("expires", "Credential lifetime, such as 30d or 12h", {"expires"}, "duration"),
        Optional::ManyToOne("description", "Credential description", {"description"}, "text")
    };
    scopeSet->optional_flags = {
        Flag::WithAliases("enforce-budget-for-local-requests", "Count local/cache hits against gateway key budgets", {"enforce-budget-for-local-requests"}),
        Flag::WithAliases("no-enforce-budget-for-local-requests", "Do not count local/cache hits against gateway key budgets", {"no-enforce-budget-for-local-requests"})
    };
    scopeSet->examples = {
        {"vh s3-gateway creds scope backup set --scope vault-allowlist", "Change credential scope mode."},
        {"vh s3-gateway creds scope backup set --enforce-budget-for-local-requests", "Enable synthetic local/cache gateway budget accounting."},
        {"vh s3-gateway creds scope backup set --no-enforce-budget-for-local-requests", "Disable synthetic local/cache gateway budget accounting."}
    };

    auto allowVault = build(scope->weak_from_this());
    allowVault->aliases = {"allow-vault"};
    allowVault->positionals = {Positional::Alias("vault", "Vault ID or name to allow", "vault")};
    allowVault->optional_flags = {
        Flag::WithAliases("list", "Allow List operations", {"list"}),
        Flag::WithAliases("read", "Allow Read operations", {"read"}),
        Flag::WithAliases("write", "Allow Write operations", {"write"}),
        Flag::WithAliases("delete", "Allow Delete operations", {"delete"}),
        Flag::WithAliases("admin", "Allow bucket admin operations", {"admin"}),
    };
    allowVault->examples = {
        {"vh s3-gateway creds scope backup allow-vault photos --read --write",
         "Allow read/write access to one vault, still subject to RBAC."}
    };

    auto revokeVault = build(scope->weak_from_this());
    revokeVault->aliases = {"revoke-vault"};
    revokeVault->positionals = {Positional::Alias("vault", "Vault ID or name to revoke", "vault")};
    revokeVault->examples = {{"vh s3-gateway creds scope backup revoke-vault photos", "Remove one vault from the allowlist."}};

    scope->subcommands = {scopeShow, scopeSet, allowVault, revokeVault};
    cmd->subcommands = {create, list, revoke, scope};
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

    auto backfill = build(cmd->weak_from_this());
    backfill->aliases = {"backfill"};
    backfill->positionals = {Positional::Alias("bucket", "Gateway bucket name", "bucket")};
    backfill->optional_flags = {
        Flag::WithAliases("calculate_etags", "Read local object bodies to calculate plaintext MD5 ETags", {"calculate-etags"})
    };
    backfill->examples = {
        {"vh s3-gateway bucket backfill photos", "Backfill gateway object metadata from local files or the remote index."}
    };

    cmd->subcommands = {list, bind, unbind, createLocal, createRemoteCache, backfill};
    return cmd;
}

std::shared_ptr<CommandUsage> budget(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = build(parent);
    cmd->aliases = {"budget", "budgets"};
    cmd->description = "Manage S3 gateway per-key and per-key/vault price budgets.";

    auto setKey = build(cmd->weak_from_this());
    setKey->aliases = {"set-key"};
    setKey->positionals = {Positional::Alias("access_key_or_name", "Access key ID or credential name", "access-key-or-name")};
    setKey->required = {Option::Single("monthly", "Monthly cost cap", "monthly", "amount")};
    setKey->optional = {
        Optional::ManyToOne("mode", "Budget mode: report, warn, or enforce", {"mode"}, "mode"),
        Optional::ManyToOne("currency", "Currency code", {"currency"}, "currency")
    };
    setKey->optional_flags = {
        Flag::WithAliases("no_require_verified_catalog", "Allow unverified provider pricing catalogs", {"no-require-verified-catalog"}),
        Flag::WithAliases("allow_stale_catalog", "Allow stale provider pricing catalogs", {"allow-stale-catalog"})
    };
    setKey->examples = {{"vh s3-gateway budget set-key backup --monthly 5 --mode enforce", "Set an admin-managed monthly cap for one key."}};

    auto setKeyVault = build(cmd->weak_from_this());
    setKeyVault->aliases = {"set-key-vault"};
    setKeyVault->positionals = {Positional::Alias("access_key_or_name", "Access key ID or credential name", "access-key-or-name")};
    setKeyVault->required = {
        Option::Single("vault", "Vault ID or name", "vault", "vault"),
        Option::Single("monthly", "Monthly cost cap", "monthly", "amount")
    };
    setKeyVault->optional = {
        Optional::ManyToOne("mode", "Budget mode: report, warn, or enforce", {"mode"}, "mode"),
        Optional::ManyToOne("currency", "Currency code", {"currency"}, "currency")
    };
    setKeyVault->optional_flags = {
        Flag::WithAliases("no_require_verified_catalog", "Allow unverified provider pricing catalogs", {"no-require-verified-catalog"}),
        Flag::WithAliases("allow_stale_catalog", "Allow stale provider pricing catalogs", {"allow-stale-catalog"})
    };
    setKeyVault->examples = {
        {"vh s3-gateway budget set-key-vault backup --vault photos --monthly 2 --mode enforce",
         "Set a monthly cap for one key/vault pair you can manage."}
    };

    auto list = build(cmd->weak_from_this());
    list->aliases = {"list", "ls"};
    list->optional = {
        Optional::ManyToOne("key", "Access key ID or credential name", {"key"}, "access-key-or-name"),
        Optional::ManyToOne("vault", "Vault ID or name", {"vault"}, "vault")
    };
    list->optional_flags = {jsonFlag};
    list->examples = {{"vh s3-gateway budget list --key backup", "List gateway budget policies for one key."}};

    auto disableKey = build(cmd->weak_from_this());
    disableKey->aliases = {"disable-key"};
    disableKey->positionals = {Positional::Alias("access_key_or_name", "Access key ID or credential name", "access-key-or-name")};
    disableKey->examples = {{"vh s3-gateway budget disable-key backup", "Disable the admin-managed per-key budget policy."}};

    auto disableKeyVault = build(cmd->weak_from_this());
    disableKeyVault->aliases = {"disable-key-vault"};
    disableKeyVault->positionals = {Positional::Alias("access_key_or_name", "Access key ID or credential name", "access-key-or-name")};
    disableKeyVault->required = {Option::Single("vault", "Vault ID or name", "vault", "vault")};
    disableKeyVault->examples = {{"vh s3-gateway budget disable-key-vault backup --vault photos", "Disable the key/vault budget policy."}};

    auto ledger = build(cmd->weak_from_this());
    ledger->aliases = {"ledger"};
    ledger->optional = {
        Optional::ManyToOne("key", "Access key ID or credential name", {"key"}, "access-key-or-name"),
        Optional::ManyToOne("vault", "Vault ID or name", {"vault"}, "vault"),
        Optional::ManyToOne("limit", "Maximum rows", {"limit"}, "N")
    };
    ledger->optional_flags = {jsonFlag};
    ledger->examples = {{"vh s3-gateway budget ledger --key backup --limit 25", "Show recent gateway budget ledger rows."}};

    auto status = build(cmd->weak_from_this());
    status->aliases = {"status"};
    status->optional = {
        Optional::ManyToOne("key", "Access key ID or credential name", {"key"}, "access-key-or-name"),
        Optional::ManyToOne("vault", "Vault ID or name", {"vault"}, "vault"),
        Optional::ManyToOne("limit", "Maximum ledger rows", {"limit"}, "N")
    };
    status->optional_flags = {jsonFlag};
    status->examples = {{"vh s3-gateway budget status --key backup", "Show policy, usage, and ledger status for one key."}};

    cmd->subcommands = {setKey, setKeyVault, list, disableKey, disableKeyVault, ledger, status};
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
                         creds(root->weak_from_this()), bucket(root->weak_from_this()), budget(root->weak_from_this())};
    root->examples = {
        {"vh s3-gateway status", "Show gateway runtime status."},
        {"vh s3-gateway creds create laptop", "Create inbound S3 credentials."},
        {"vh s3-gateway bucket bind photos --vault 12", "Expose an existing vault as a bucket."},
        {"vh s3-gateway budget set-key backup --monthly 5 --mode enforce", "Enforce a monthly gateway budget."}
    };
    book->root = root;
    return book;
}

} // namespace vh::protocols::shell::s3gateway
