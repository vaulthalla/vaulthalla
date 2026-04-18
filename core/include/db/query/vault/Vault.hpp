#pragma once

#include "db/model/ListQueryParams.hpp"

#include <memory>
#include <vector>
#include <string>
#include <optional>

namespace vh::vault::model { struct Vault; enum class VaultType; }
namespace vh::sync::model { struct Policy; }

namespace vh::db::query::vault {

class Vault {
    using V = vh::vault::model::Vault;
    using P = vh::sync::model::Policy;
    using VaultPtr = std::shared_ptr<V>;
    using PolicyPtr = std::shared_ptr<P>;
    using VaultT = vh::vault::model::VaultType;

public:
    Vault() = default;

    static unsigned int upsertVault(const VaultPtr& vault,
                                 const PolicyPtr& sync = nullptr);
    static void removeVault(unsigned int vaultId);
    static VaultPtr getVault(unsigned int vaultID);
    static VaultPtr getVault(const std::string& name, unsigned int ownerId);
    static std::vector<VaultPtr> listVaults(const std::optional<VaultT>& type = std::nullopt, model::ListQueryParams&& params = {});
    static std::vector<VaultPtr> listUserVaults(unsigned int userId, const std::optional<VaultT>& type = std::nullopt, model::ListQueryParams&& params = {});

    static void updateVaultSync(const PolicyPtr& sync, const VaultT& type);

    [[nodiscard]] static bool vaultExists(const std::string& name, unsigned int ownerId);

    [[nodiscard]] static bool vaultRootExists(unsigned int vaultId);

    [[nodiscard]] static unsigned int maxVaultId();

    static bool localDiskVaultExists();

    [[nodiscard]] static std::string getVaultMountPoint(unsigned int vaultId);

    static std::string getVaultOwnersName(unsigned int vaultId);
    [[nodiscard]] static unsigned int getVaultOwnerId(unsigned int vaultId);
};

}
