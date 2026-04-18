#pragma once

#include <memory>
#include <vector>

namespace vh::sync::model { struct Waiver; }

namespace vh::db::query::vault {

struct Waiver {
    static void addWaiver(const std::shared_ptr<vh::sync::model::Waiver>& waiver);

    static std::vector<std::shared_ptr<vh::sync::model::Waiver>> getAllWaivers();

    static std::vector<std::shared_ptr<vh::sync::model::Waiver>> getWaiversByUser(unsigned int userId);

    static std::vector<std::shared_ptr<vh::sync::model::Waiver>> getWaiversByVault(unsigned int vaultId);
};

}
