#pragma once

#include <memory>
#include <string>
#include <pqxx/connection>

namespace vh::crypto::secrets { class TPMKeyProvider; }

namespace vh::db {

class Connection {
  public:
    Connection();
    ~Connection();

    [[nodiscard]] pqxx::connection& get() const;

    void initPrepared() const;

  private:
    std::unique_ptr<crypto::secrets::TPMKeyProvider> tpmKeyProvider_;
    std::string DB_CONNECTION_STR;
    std::unique_ptr<pqxx::connection> conn_;

    // Auth
    void initPreparedUsers() const;
    void initPreparedGroups() const;
    void initPreparedRefreshTokens() const;

    // RBAC
    void initPreparedPermissions() const;
    void initPreparedAdminRoles() const;
    void initPreparedAdminRoleAssignments() const;
    void initPreparedGlobalVaultRoles() const;
    void initPreparedVaultRoles() const;
    void initPreparedVaultRoleAssignments() const;
    void initPreparedPermOverrides() const;

    // Vaults
    void initPreparedVaults() const;
    void initPreparedVaultKeys() const;
    void initPreparedAPIKeys() const;

    // Sync
    void initPreparedSync() const;
    void initPreparedSyncEvents() const;
    void initPreparedSyncThroughput() const;
    void initPreparedSyncConflicts() const;
    void initPreparedSyncConflictArtifacts() const;
    void initPreparedSyncConflictReasons() const;

    // Filesystem
    void initPreparedFsEntries() const;
    void initPreparedFiles() const;
    void initPreparedDirectories() const;
    void initPreparedOperations() const;
    void initPreparedCache() const;

    // Admin
    void initPreparedSecrets() const;
    void initPreparedWaivers() const;
};

struct PathPatterns {
    std::string like;
    std::string not_like;
};

inline PathPatterns computePatterns(const std::string& absPath, const bool recursive) {
    const std::string base = (absPath == "/") ? "" : absPath;
    if (recursive) return {base + "/%", ""};
    return {base + "/%", base + "/%/%"};
}

}
