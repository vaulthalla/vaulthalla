#pragma once

#define FUSE_USE_VERSION 35

#include <mutex>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <fuse3/fuse_lowlevel.h>

namespace vh::identities { struct User; }
namespace vh::vault::model { struct Vault; }
namespace vh::sync::model { struct Policy; }

namespace vh::storage {

struct Engine;

class Manager {
public:
    Manager();

    void initStorageEngines();

    std::vector<std::shared_ptr<Engine>> getEngines() const;

    std::shared_ptr<Engine> resolveStorageEngine(const std::filesystem::path& fusePath) const;

    void initUserStorage(const std::shared_ptr<identities::User>& user);

    std::shared_ptr<vault::model::Vault> addVault(std::shared_ptr<vault::model::Vault> vault,
                                           const std::shared_ptr<sync::model::Policy>& sync = nullptr);

    void updateVault(const std::shared_ptr<vault::model::Vault>& vault);

    void removeVault(unsigned int vaultId);

    std::shared_ptr<vault::model::Vault> getVault(unsigned int vaultId) const;

    std::shared_ptr<Engine> getEngine(unsigned int id) const;

    void registerOpenHandle(fuse_ino_t);

    void closeOpenHandle(fuse_ino_t);

    [[nodiscard]] unsigned int getOpenHandleCount(fuse_ino_t ino) const;

private:
    mutable std::mutex mutex_, openHandleMutex_;
    std::pmr::unordered_map<std::string, std::shared_ptr<Engine>> engines_;
    std::unordered_map<unsigned int, std::shared_ptr<Engine>> vaultToEngine_;
    std::unordered_map<fuse_ino_t, int> openHandleCounts_;
};

}
