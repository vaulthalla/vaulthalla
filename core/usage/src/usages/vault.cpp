#include "usages.hpp"
#include "CommandUsage.hpp"

using namespace vh::protocols::shell;

namespace vh::protocols::shell::vault {

static std::shared_ptr<CommandUsage> buildBaseUsage(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = std::make_shared<CommandUsage>();
    cmd->parent = parent;
    return cmd;
}

static const Positional vaultPos = Positional::WithAliases("vault", "Name or ID of the vault", {"name", "id"});

static const Optional localConflictOpt = Optional::Multi("local_conflict",
                                                         "Conflict resolution strategy for local vaults",
                                                         {"on-sync-conflict", "conflict"},
                                                         {"overwrite", "keep_both", "ask"}, "overwrite");
static const Optional syncStrategyOpt = Optional::Multi("sync_strategy", "Sync strategy for S3 vaults",
                                                        {"sync-strategy", "strategy"}, {"cache", "sync", "mirror"});
static const Optional s3ConflictOpt = Optional::Multi("s3_conflict", "Conflict resolution strategy",
                                                      {"on-sync-conflict", "conflict"},
                                                      {"keep_local", "keep_remote", "ask"});

static const auto intervalOpt = Optional::ManyToOne("interval", "Sync interval in seconds (default 5m)", {"interval", "sync-interval"}, "interval");

static const auto interactiveFlag = Flag::WithAliases("interactive_mode",
                                                      "Run in interactive mode, prompting for missing information",
                                                      {"interactive", "i"});

static const auto descriptionOpt = Optional::ManyToOne("description", "Description of the vault", {"description", "desc", "d"},
                                                       "description");

static const auto quotaOpt = Optional::Multi(
    "quota", "Storage quota (e.g. 10G, 500M). Use 'unlimited' to remove quota.", {"quota", "q"},
    {"size", "unlimited"});

static const auto vaultType = Flag::WithAliases("type", "Type of vault to create", {"local", "s3"});

static const auto owner = Optional::Multi("owner", "User ID or username of the vault owner (required if using name)",
                                          {"owner", "o"},
                                          {"id", "name"});

static const auto vaultOrAll = Positional::WithAliases(
    "vault", "Single vault: ID or name of the vault or 'all' for all vaults",
    {"id", "name", "all"});

static const auto syncNowFlag = Flag::WithAliases("sync_now", "Immediately synchronize the vault after key rotation",
                                                  {"now", "sync-now", "sync"});

static const auto roleId = Positional::Alias("role_id", "ID of the role to override", "id");

static const auto bitPosition = Positional::Same("bit_position", "Bit position of the permission override to update");

static const auto enableFlag = Flag::Alias("enable", "Enable the override (default)", "enable");
static const auto disableFlag = Flag::Alias("disable", "Disable the override", "disable");

static const auto regexPattern = Optional::Single("regex_pattern",
                                                  "Regex pattern to scope the override to specific paths", "pattern",
                                                  "regex");

static const auto localFlag = Flag::Alias("local_filter", "Show only local vaults", "local");
static const auto s3Flag = Flag::Alias("s3_filter", "Show only S3-backed vaults", "s3");

static const auto apiKeyOpt = Optional::OneToMany("s3_api_key", "Name or ID of the API key to access the S3 bucket",
                                                  "api-key", {"name", "id"});

static const auto s3BucketOpt = Optional::Single("s3_bucket", "Name of the S3 bucket", "bucket", "name");

static const auto enableS3Encryption = Flag::On("enable_s3_encrypt",
                                                "Enable upstream encryption for S3 vaults. This is the default.",
                                                {"encrypt"});
static const auto disableS3Encryption = Flag::Off("disable_s3_encrypt", "Disable upstream encryption for S3 vaults.",
                                                  {"no-encrypt"});
static const auto acceptOverwriteWaiver = Flag::Alias("accept_overwrite_waiver",
                                                      "Acknowledge the risks of enabling encryption on an upstream s3 bucket with existing files.",
                                                      "accept-overwrite-waiver");
static const auto acceptDecryptionWaiver = Flag::Alias("accept_decryption_waiver",
                                                       "Acknowledge the risks of disabling encryption on an upstream s3 bucket with existing encrypted files.",
                                                       "accept-decryption-waiver");

static const GroupedOptions localVaultOpts("Local Vault Options", {localConflictOpt});

static const GroupedOptions s3VaultOpts("S3 Vault Options", {
                                            apiKeyOpt,
                                            s3BucketOpt,
                                            syncStrategyOpt,
                                            s3ConflictOpt,
                                            enableS3Encryption,
                                            disableS3Encryption,
                                            acceptOverwriteWaiver,
                                            acceptDecryptionWaiver
                                        });

static std::shared_ptr<CommandUsage> create(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"create", "new", "add", "mk"};
    cmd->description = "Create a new vault. Supports local and S3-backed vaults.";
    cmd->positionals = {vaultPos};
    cmd->required_flags = {vaultType};
    cmd->optional_flags = {interactiveFlag};
    cmd->optional = {descriptionOpt, quotaOpt, intervalOpt,  owner};
    cmd->groups = {localVaultOpts, s3VaultOpts};
    cmd->examples = {
        {"vh vault create myvault --local --desc \"My Local Vault\" --quota 10G",
         "Create a local vault with a 10GB quota."},
        {"vh vault create s3vault --s3 --api-key myapikey --bucket mybucket", "Create an S3-backed vault."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> update(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"update", "set", "modify", "edit"};
    cmd->description = "Update properties of an existing vault.";
    cmd->positionals = {vaultPos};
    cmd->optional = {descriptionOpt, quotaOpt, intervalOpt, owner};
    cmd->optional_flags = {interactiveFlag};
    cmd->groups = {localVaultOpts, s3VaultOpts};
    cmd->examples = {
        {"vh vault update 42 --desc \"Updated Description\" --quota 20G",
         "Update the description and quota of the vault with ID 42."},
        {"vh vault update myvault --owner bob --api-key newkey --bucket newbucket --sync-strategy mirror "
         "--on-sync-conflict keep_remote --owner alice",
         "Update multiple properties of the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> remove(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"delete", "remove", "del", "rm"};
    cmd->description = "Delete an existing vault by ID or name.";
    cmd->positionals = {vaultPos};
    cmd->optional = {owner};
    cmd->examples = {
        {"vh vault delete 42", "Delete the vault with ID 42."},
        {"vh vault delete myvault --owner alice", "Delete the vault named 'myvault' owned by user 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> info(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"info", "show", "get"};
    cmd->description = "Display detailed information about a vault.";
    cmd->positionals = {vaultPos};
    cmd->optional = {owner};
    cmd->examples = {
        {"vh vault info 42", "Show information for the vault with ID 42."},
        {"vh vault info myvault --owner alice", "Show information for the vault named 'myvault' owned by user 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> list(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"list", "ls"};
    cmd->description = "List all vaults accessible to the current user.";
    cmd->required_flags = {localFlag, s3Flag, jsonFlag};
    cmd->optional = {limitOpt};
    cmd->examples = {
        {"vh vaults", "List all vaults accessible to the current user."},
        {"vh vaults --local", "List only local vaults."},
        {"vh vaults --s3 --limit 5", "List up to 5 S3-backed vaults."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_assign(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"assign"};
    cmd->description = "Assign a role to a user or group for a specific vault.";
    cmd->positionals = {vaultPos, roleId};
    cmd->required = {subjectOption};
    cmd->optional = {owner};
    cmd->examples = {
        {"vh vault role assign 42 read-only bob", "Add user 'bob' to the 'read-only' role for the vault with ID 42."},
        {"vh vault role assign myvault read-write developers --owner alice",
         "Add group 'developers' to the 'read-write' role for the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_unassign(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"unassign", "rm-assn"};
    cmd->description = "Remove a role assignment from a user or group for a specific vault.";
    cmd->positionals = {vaultPos, roleId};
    cmd->required = {subjectOption};
    cmd->optional = {owner};
    cmd->examples = {
        {"vh vault role unassign 42 read-only bob",
         "Remove user 'bob' from the 'read-only' role for the vault with ID 42."},
        {"vh vault role unassign myvault read-write developers --owner alice",
         "Remove group 'developers' from the 'read-write' role for the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_override_add(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"add", "new", "create", "mk"};
    cmd->description = "Add a permission override for a user or group in a specific vault role.";
    cmd->positionals = {vaultPos, roleId};
    cmd->required_flags = {permissionsFlags};
    cmd->required = {subjectOption};
    cmd->optional_flags = {enableFlag, disableFlag};
    cmd->optional = {regexPattern, owner};
    cmd->examples = {
        {R"(vh vault role override 42 read-only bob --download allow --pattern ".*\.pdf$")",
         "Allow user 'bob' to download PDF files in the vault with ID 42, overriding the 'read-only' role."},
        {R"(vh vault role override myvault read-write developers --gid 1001 --delete deny --pattern "^/sensitive/")",
         "Deny group with GID 1001 from deleting files in the '/sensitive/' directory in the vault named 'myvault'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_override_update(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"update", "set", "modify", "edit"};
    cmd->description = "Update a permission override for a user or group in a specific vault role.";
    cmd->positionals = {vaultPos, roleId, bitPosition};
    cmd->optional_flags = {enableFlag, disableFlag};
    cmd->optional = {regexPattern, owner};
    cmd->examples = {
        {R"(vh vault role override update 42 read-only 7 --deny --pattern ".*\.exe$")",
         "Update override ID 7 for user 'bob' in the vault with ID 42 to deny downloading .exe files."},
        {R"(vh vault role override update myvault read-write 3 --enabled false --owner alice)",
         "Disable override ID 3 for group 'developers' in the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_override_remove(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"remove", "del", "rm"};
    cmd->description = "Remove a permission override (by bit position) from a user or group in a specific vault role.";
    cmd->positionals = {vaultPos, roleId, bitPosition};
    cmd->required = {
        Option::Multi("subject", "Specify the user or group whose override to remove", {"user", "u", "group", "g"},
                      {"id", "name"})
    };
    cmd->optional = {
        Optional::Multi("owner", "User ID or username of the vault owner (required if using name)", {"owner", "o"},
                        {"id", "name"})
    };
    cmd->examples = {
        {"vh vault role override remove 42 read-only 5 -u bob",
         "Remove the bit-5 override for user 'bob' in vault 42 on role 'read-only'."},
        {"vh vault role override rm myvault read-write 3 --group developers --owner alice",
         "Remove the bit-3 override for group 'developers' in 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_override_list(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"list", "ls"};
    cmd->description = "List permission overrides for a user or group in a specific vault role.";
    cmd->positionals = {vaultPos, roleId};
    cmd->required = {subjectOption};
    cmd->optional = {owner};
    cmd->examples = {
        {"vh vault role override list 42 read-only -u bob",
         "List all overrides for user 'bob' in role 'read-only' on vault 42."},
        {"vh vault role override ls myvault read-write --group developers --owner alice",
         "List all overrides for group 'developers' in 'myvault' (owner 'alice') on role 'read-write'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_override(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"override", "o"};
    cmd->description = "Manage permission overrides for users or groups in a specific vault role.";
    cmd->examples = {
        {R"(vh vault role override 42 read-only -u bob --download allow --pattern ".*\.pdf$")",
         "Allow user 'bob' to download PDF files in vault 42, overriding 'read-only'."},
        {R"(vh vault role override myvault read-write --group developers --delete deny --pattern "^/sensitive/" --owner alice)",
         "Deny 'developers' from deleting files under '/sensitive/' in 'myvault' (owner 'alice')."}
    };
    cmd->subcommands = {
        role_override_add(cmd->weak_from_this()),
        role_override_update(cmd->weak_from_this()),
        role_override_remove(cmd->weak_from_this()),
        role_override_list(cmd->weak_from_this())
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> role_list(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"list", "ls"};
    cmd->description = "List all role assignments for a specific vault.";
    cmd->positionals = {vaultPos};
    cmd->optional = {owner};
    cmd->examples = {
        {"vh vault role list 42", "List all role assignments for the vault with ID 42."},
        {"vh vault role list myvault --owner alice",
         "List all role assignments for the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> vrole(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"role", "r"};
    cmd->pluralAliasImpliesList = true;
    cmd->description = "Manage vault role assignments and permission overrides.";
    cmd->examples = {
        {"vh vault role assign 42 read-only bob", "Add user 'bob' to the 'read-only' role for the vault with ID 42."},
        {"vh vault role remove myvault read-write developers --owner alice",
         "Remove group 'developers' from the 'read-write' role for the vault named 'myvault' owned by 'alice'."},
        {R"(vh vault role override 42 read-only bob --download allow --pattern ".*\.pdf$")",
         "Allow user 'bob' to download PDF files in the vault with ID 42, overriding the 'read-only' role."},
        {"vh vault role list myvault --owner alice",
         "List all role assignments for the vault named 'myvault' owned by 'alice'."}
    };
    cmd->subcommands = {
        role_list(cmd->weak_from_this()),
        role_override(cmd->weak_from_this()),
        role_assign(cmd->weak_from_this()),
        role_unassign(cmd->weak_from_this())
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> key_export(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"export"};
    cmd->description = "Export an encryption key for a specific vault.";
    cmd->positionals = {vaultOrAll};
    cmd->optional = {gpgRecipient, outputFile, owner};
    cmd->examples = {
        {"vh vault keys export 42 --output keyfile.pem --recipient ABCDEF1234567890",
         "Export the encryption key for the vault with ID 42 to 'keyfile.pem', encrypted for the GPG recipient with fingerprint 'ABCDEF1234567890'."},
        {"vh vault keys export myvault --owner alice --output myvault_key.pem",
         "Export the encryption key for the vault named 'myvault' owned by 'alice' to 'myvault_key.pem' (unencrypted)."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> key_rotate(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"rotate", "renew"};
    cmd->description = "Rotate the encryption key for a specific vault.";
    cmd->positionals = {vaultOrAll};
    cmd->optional = {owner};
    cmd->optional_flags = {syncNowFlag};
    cmd->examples = {
        {"vh vault keys rotate 42 --sync-now",
         "Rotate the encryption key for the vault with ID 42 and immediately sync."},
        {"vh vault keys rotate myvault --owner alice",
         "Rotate the encryption key for the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> key(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"key", "k"};
    cmd->pluralAliasImpliesList = true;
    cmd->description = "Manage encryption keys for vaults.";
    cmd->examples = {
        {"vh vault keys export 42 --output keyfile.pem --recipient ABCDEF1234567890",
         "Export the encryption key for the vault with ID 42 to 'keyfile.pem', encrypted for the GPG recipient with fingerprint 'ABCDEF1234567890'."},
        {"vh vault keys rotate myvault --owner alice",
         "Rotate the encryption key for the vault named 'myvault' owned by 'alice'."}
    };
    cmd->subcommands = {
        key_export(cmd->weak_from_this()),
        key_rotate(cmd->weak_from_this())
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> sync_info(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"info", "show", "get"};
    cmd->description = "Display the current synchronization settings for a specific vault.";
    cmd->positionals = {vaultPos};
    cmd->optional = {owner};
    cmd->examples = {
        {"vh vault sync info 42", "Show sync configuration for the vault with ID 42."},
        {"vh vault sync info myvault --owner alice",
         "Show sync configuration for the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> sync_set(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"set", "update", "modify", "edit"};
    cmd->description = "Set or update synchronization settings for a specific vault.";
    cmd->positionals = {vaultPos};
    cmd->optional = {owner};
    cmd->groups = {
        {"Local Vault Options", {
             localConflictOpt
         }},
        {"S3 Vault Options", {
             syncStrategyOpt,
             s3ConflictOpt
         }}
    };
    cmd->examples = {
        {"vh vault sync set 42 --sync-strategy mirror --on-sync-conflict keep_remote",
         "Set sync configuration for the vault with ID 42."},
        {"vh vault sync set myvault --sync-strategy cache --owner alice",
         "Set sync configuration for the vault named 'myvault' owned by 'alice'."}
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> sync(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"sync", "s"};
    cmd->description = "Manage vault synchronization settings and operations.";
    cmd->positionals = {vaultPos};
    cmd->examples = {
        {"vh vault sync 42", "Manually trigger a sync for the vault with ID 42."},
        {"vh vault sync info 42", "Show sync configuration for the vault with ID 42."},
        {"vh vault sync set 42 --sync-strategy mirror --on-sync-conflict keep_remote",
         "Set sync configuration for the vault with ID 42."},
        {"vh vault sync update 42 --sync-strategy cache", "Update the sync strategy for the vault with ID 42."}
    };
    cmd->subcommands = {
        sync_info(cmd->weak_from_this()),
        sync_set(cmd->weak_from_this())
    };
    return cmd;
}

static std::shared_ptr<CommandUsage> base(const std::weak_ptr<CommandUsage>& parent) {
    auto cmd = buildBaseUsage(parent);
    cmd->aliases = {"vault", "v"};
    cmd->description = "Manage a single vault.";
    cmd->pluralAliasImpliesList = true;

    // ---- build direct children first (we'll mine their subcommands next) ----
    const auto listCmd = list(cmd->weak_from_this());
    const auto createCmd = create(cmd->weak_from_this());
    const auto removeCmd = remove(cmd->weak_from_this());
    const auto infoCmd = info(cmd->weak_from_this());
    const auto updateCmd = update(cmd->weak_from_this());

    const auto vroleCmd = vrole(cmd->weak_from_this());
    const auto keyCmd = key(cmd->weak_from_this());
    const auto syncCmd = sync(cmd->weak_from_this()); // executable (manual sync) + has subcommands

    // ---- helpers to pluck children by alias from a parent we just built ----
    auto findChild = [](const std::shared_ptr<CommandUsage>& parentCmd, std::string_view alias)
        -> std::shared_ptr<CommandUsage> {
        for (const auto& sc : parentCmd->subcommands) {
            if (sc && sc->matches(std::string(alias))) return sc;
        }
        return nullptr;
    };

    // ---- dig into vrole -> (list | override | assign | unassign) ----
    const auto roleListCmd = findChild(vroleCmd, "list");
    const auto roleOverrideCmd = findChild(vroleCmd, "override");
    const auto roleAssignCmd = findChild(vroleCmd, "assign");
    const auto roleUnassignCmd = findChild(vroleCmd, "unassign");

    // and into override -> (add | update | remove | list)
    std::shared_ptr<CommandUsage> ovAddCmd, ovUpdCmd, ovRmCmd, ovListCmd;
    if (roleOverrideCmd) {
        ovAddCmd = findChild(roleOverrideCmd, "add");
        ovUpdCmd = findChild(roleOverrideCmd, "update");
        ovRmCmd = findChild(roleOverrideCmd, "remove");
        ovListCmd = findChild(roleOverrideCmd, "list");
    }

    // ---- dig into key -> (export | rotate) ----
    const auto keyExportCmd = findChild(keyCmd, "export");
    const auto keyRotateCmd = findChild(keyCmd, "rotate");

    // ---- dig into sync -> (info | set) ----
    const auto syncInfoCmd = findChild(syncCmd, "info");
    const auto syncSetCmd = findChild(syncCmd, "set");

    // -- Define reusable TestCommandUsage handles
    const auto t_create = TestCommandUsage::Single(createCmd);
    const auto t_create_many = TestCommandUsage::Multiple(createCmd);
    const auto t_remove = TestCommandUsage::Single(removeCmd);
    const auto t_remove0 = TestCommandUsage::Multiple(removeCmd, 0, 0);

    const auto t_info = TestCommandUsage::Single(infoCmd);
    const auto t_update = TestCommandUsage::Single(updateCmd);

    const auto t_sync = TestCommandUsage::Single(syncCmd);
    const auto t_sync_info = TestCommandUsage::Single(syncInfoCmd);
    const auto t_sync_set = TestCommandUsage::Single(syncSetCmd);

    const auto t_key_export = TestCommandUsage::Single(keyExportCmd);
    const auto t_key_rotate = TestCommandUsage::Single(keyRotateCmd);

    const auto t_role_list = TestCommandUsage::Single(roleListCmd);
    const auto t_role_assign = TestCommandUsage::Single(roleAssignCmd);
    const auto t_role_assign_m = TestCommandUsage::Multiple(roleAssignCmd);
    const auto t_role_unassign = TestCommandUsage::Single(roleUnassignCmd);
    const auto t_role_unassign_m = TestCommandUsage::Multiple(roleUnassignCmd);

    const auto t_ov_add = TestCommandUsage::Single(ovAddCmd);
    const auto t_ov_add_m = TestCommandUsage::Multiple(ovAddCmd);
    const auto t_ov_upd = TestCommandUsage::Single(ovUpdCmd);
    const auto t_ov_rm = TestCommandUsage::Single(ovRmCmd);
    const auto t_ov_rm_m = TestCommandUsage::Multiple(ovRmCmd);
    const auto t_ov_list = TestCommandUsage::Single(ovListCmd);

    // -- Assign lifecycle tests
    listCmd->test_usage.setup = {t_create_many};
    listCmd->test_usage.teardown = {t_remove0};

    createCmd->test_usage.lifecycle = {
        t_info, t_update, t_sync_set, t_sync_info, t_key_export
    };
    createCmd->test_usage.teardown = {t_remove};

    removeCmd->test_usage.setup = {t_create};

    infoCmd->test_usage.setup = {t_create};
    infoCmd->test_usage.teardown = {t_remove};

    updateCmd->test_usage.setup = {t_create};
    updateCmd->test_usage.teardown = {t_remove};

    syncCmd->test_usage.setup = {t_create};
    syncCmd->test_usage.teardown = {t_remove};

    syncInfoCmd->test_usage.setup = {t_create};
    syncInfoCmd->test_usage.teardown = {t_remove};

    syncSetCmd->test_usage.setup = {t_create};
    syncSetCmd->test_usage.teardown = {t_remove};

    keyExportCmd->test_usage.setup = {t_create};
    keyExportCmd->test_usage.teardown = {t_remove};

    keyRotateCmd->test_usage.setup = {t_create};
    keyRotateCmd->test_usage.lifecycle = {t_key_export};
    keyRotateCmd->test_usage.teardown = {t_remove};

    roleAssignCmd->test_usage.setup = {t_create};
    roleAssignCmd->test_usage.lifecycle = {t_role_list};
    roleAssignCmd->test_usage.teardown = {t_role_unassign};

    roleUnassignCmd->test_usage.setup = {t_create, t_role_assign};
    roleUnassignCmd->test_usage.teardown = {t_remove};

    roleListCmd->test_usage.setup = {t_create, t_role_assign_m};
    roleListCmd->test_usage.teardown = {t_role_unassign_m, t_remove};

    ovAddCmd->test_usage.setup = {t_create, t_role_assign};
    ovAddCmd->test_usage.lifecycle = {t_ov_list};
    ovAddCmd->test_usage.teardown = {t_ov_rm, t_role_unassign, t_remove};

    ovUpdCmd->test_usage.setup = {t_create, t_role_assign, t_ov_add};
    ovUpdCmd->test_usage.lifecycle = {t_ov_list};
    ovUpdCmd->test_usage.teardown = {t_ov_rm, t_role_unassign, t_remove};

    ovRmCmd->test_usage.setup = {t_create, t_role_assign, t_ov_add};
    ovRmCmd->test_usage.teardown = {t_role_unassign, t_remove};

    ovListCmd->test_usage.setup = {t_create, t_role_assign, t_ov_add_m};
    ovListCmd->test_usage.teardown = {t_ov_rm_m, t_role_unassign, t_remove};

    // ---- examples (unchanged) ----
    cmd->examples = {
        {"vh vault create myvault --local --desc \"My Local Vault\" --quota 10G",
         "Create a local vault with a 10GB quota."},
        {"vh vault update 42 --desc \"Updated Description\" --quota 20G",
         "Update the description and quota of the vault with ID 42."},
        {"vh vault delete myvault --owner alice", "Delete the vault named 'myvault' owned by user 'alice'."},
        {"vh vault info 42", "Show information for the vault with ID 42."},
        {"vh vaults", "List all vaults accessible to the current user."},
        {"vh vault role assign 42 read-only bob", "Add user 'bob' to the 'read-only' role for the vault with ID 42."},
        {"vh vault keys export 42 --output keyfile.pem --recipient ABCDEF1234567890",
         "Export the encryption key for the vault with ID 42 to 'keyfile.pem', encrypted for the GPG recipient with fingerprint 'ABCDEF1234567890'."},
        {"vh vault sync 42", "Manually trigger a sync for the vault with ID 42."}
    };

    // ---- finalize tree ----
    cmd->subcommands = {
        listCmd,
        createCmd,
        removeCmd,
        infoCmd,
        updateCmd,
        vroleCmd,
        keyCmd,
        syncCmd
    };

    return cmd;
}

std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = std::make_shared<CommandBook>();
    cmd->title = "Vault Commands";
    cmd->root = base(parent);
    return cmd;
}

}