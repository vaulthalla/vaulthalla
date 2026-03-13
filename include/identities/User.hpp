#pragma once

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; class result; }
namespace vh::rbac::role { struct Admin; struct Vault; }

namespace vh::identities {

struct User : std::enable_shared_from_this<User> {
    static constexpr uint16_t ADMIN_MASK = 0xFFFE;

    unsigned int id{};
    std::optional<unsigned int> linux_uid{std::nullopt}, last_modified_by{std::nullopt};
    std::string name{}, password_hash{};
    std::optional<std::string> email{std::nullopt};
    std::time_t created_at{}, updated_at{};
    std::optional<std::time_t> last_login{};
    bool is_active{true};
    std::shared_ptr<rbac::role::Admin> role{};
    std::unordered_map<unsigned int, std::shared_ptr<rbac::role::Vault>> vault_roles{}, group_roles{};
    mutable std::mutex mutex_{};

    User();
    explicit User(std::string name, std::string email = "", bool isActive = true);
    explicit User(const pqxx::row& row);
    User(const pqxx::row& user, const pqxx::row& role, const pqxx::result& globalVaultRoles, const pqxx::result& vaultRoles, const pqxx::result& overrides);

    bool operator==(const User& other) const;
    bool operator!=(const User& other) const;

    [[nodiscard]] std::shared_ptr<rbac::role::Vault> getRole(unsigned int vaultId) const;

    void updateUser(const nlohmann::json& j);
    void setPasswordHash(const std::string& hash);

    [[nodiscard]] bool isAdmin() const;
    [[nodiscard]] bool isSuperAdmin() const;

    // Admin checks
    [[nodiscard]] bool canManageEncryptionKeys() const;
    [[nodiscard]] bool canManageAdmins() const;
    [[nodiscard]] bool canManageUsers() const;
    [[nodiscard]] bool canManageGroups() const;
    [[nodiscard]] bool canManageRoles() const;
    [[nodiscard]] bool canManageSettings() const;
    [[nodiscard]] bool canManageVaults() const;
    [[nodiscard]] bool canAccessAuditLog() const;
    [[nodiscard]] bool canManageAPIKeys() const;
    [[nodiscard]] bool canCreateVaults() const;

    // Vault permissions
    [[nodiscard]] bool canManageVault(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canManageVaultAccess(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canManageVaultTags(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canManageVaultMetadata(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canManageVaultVersions(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canManageVaultFileLocks(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canShareVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canSyncVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canCreateVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canDownloadVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canDeleteVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canRenameVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canMoveVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
    [[nodiscard]] bool canListVaultData(unsigned int vaultId, const std::filesystem::path& path = {}) const;
};


void to_json(nlohmann::json& j, const User& u);
void from_json(const nlohmann::json& j, User& u);

nlohmann::json to_public_json(const std::shared_ptr<User>& u);
nlohmann::json to_public_json(const std::vector<std::shared_ptr<User>>& u);

nlohmann::json to_json(const std::vector<std::shared_ptr<User>>& users);
nlohmann::json to_json(const std::shared_ptr<User>& user);

std::string to_string(const std::shared_ptr<User>& user);
std::string to_string(const std::vector<std::shared_ptr<User>>& users);

}
