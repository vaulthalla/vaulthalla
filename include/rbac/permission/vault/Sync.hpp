#pragma once

#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/vault/sync/Config.hpp"
#include "rbac/permission/vault/sync/Action.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault {

struct Sync final : Module<uint32_t> {
    static constexpr const auto* ModuleName = "Sync";

    sync::Config config;
    sync::Action action;

    Sync() = default;
    explicit Sync(const Mask& mask) { fromMask(mask); }

    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] const char* name() const override { return ModuleName; }
    [[nodiscard]] uint32_t toMask() const override { return pack(config, action); }
    void fromMask(const Mask mask) override { unpack(mask, config, action); }

    [[nodiscard]] bool canViewConfig() const noexcept { return config.canView(); }
    [[nodiscard]] bool canEditConfig() const noexcept { return config.canEdit(); }
    [[nodiscard]] bool canTriggerSync() const noexcept { return action.canTrigger(); }
};

void to_json(nlohmann::json& j, const Sync& v);
void from_json(const nlohmann::json& j, Sync& v);

}