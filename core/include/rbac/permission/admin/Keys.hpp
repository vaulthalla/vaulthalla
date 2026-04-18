#pragma once

#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/admin/keys/API.hpp"
#include "rbac/permission/admin/keys/Encryption.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin {

struct Keys final : Module<uint32_t> {
    static constexpr const auto* ModuleName = "keys";

    keys::APIKeys apiKeys{};
    keys::EncryptionKey encryptionKeys{};

    Keys() = default;
    explicit Keys(const Mask& mask) { fromMask(mask); }

    [[nodiscard]] std::string toFlagsString() const override;
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] std::vector<std::string> getFlags() const override {
        return Module::getFlags(apiKeys.self, apiKeys.admin, apiKeys.user, encryptionKeys);
    }

    [[nodiscard]] const char* name() const override { return ModuleName; }
    [[nodiscard]] uint32_t toMask() const override;
    void fromMask(Mask mask) override;

    [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
        return packAndExportPerms(
            mount("admin.keys.api.self", apiKeys.self),
            mount("admin.keys.api.admin", apiKeys.admin),
            mount("admin.keys.api.user", apiKeys.user),
            mount("admin.keys.encryption", encryptionKeys)
        );
    }

    static Keys None() {
        Keys k;
        k.apiKeys = keys::APIKeys::None();
        k.encryptionKeys = keys::EncryptionKey::None();
        return k;
    }

    static Keys ViewOnly() {
        Keys k;
        k.apiKeys = keys::APIKeys::ViewOnly();
        k.encryptionKeys = keys::EncryptionKey::ViewOnly();
        return k;
    }

    static Keys APIKeyOperator() {
        Keys k;
        k.apiKeys = keys::APIKeys::MixedOperator();
        k.encryptionKeys = keys::EncryptionKey::None();
        return k;
    }

    static Keys APIKeyManager() {
        Keys k;
        k.apiKeys = keys::APIKeys::Full();
        k.encryptionKeys = keys::EncryptionKey::None();
        return k;
    }

    static Keys KeyCustodian() {
        Keys k;
        k.apiKeys = keys::APIKeys::None();
        k.encryptionKeys = keys::EncryptionKey::Custodian();
        return k;
    }

    static Keys PlatformOperator() {
        Keys k;
        k.apiKeys = keys::APIKeys::MixedOperator();
        k.encryptionKeys = keys::EncryptionKey::ViewOnly();
        return k;
    }

    static Keys SecurityAdmin() {
        Keys k;
        k.apiKeys = keys::APIKeys::AdminManager();
        k.encryptionKeys = keys::EncryptionKey::Custodian();
        return k;
    }

    static Keys APIKeysAll() {
        Keys k;
        k.apiKeys = keys::APIKeys::Full();
        k.encryptionKeys = keys::EncryptionKey::None();
        return k;
    }

    static Keys EncryptionKeysAll() {
        Keys k;
        k.apiKeys = keys::APIKeys::None();
        k.encryptionKeys = keys::EncryptionKey::Full();
        return k;
    }

    static Keys Full() {
        Keys k;
        k.apiKeys = keys::APIKeys::Full();
        k.encryptionKeys = keys::EncryptionKey::Full();
        return k;
    }

    static Keys Custom(keys::APIKeys api_keys, keys::EncryptionKey encryption_keys) {
        Keys k;
        k.apiKeys = std::move(api_keys);
        k.encryptionKeys = std::move(encryption_keys);
        return k;
    }

    static Keys Custom(
        keys::APIKeyBase self,
        keys::APIKeyBase admins,
        keys::APIKeyBase users,
        keys::EncryptionKey encryption_keys
    ) {
        Keys k;
        k.apiKeys = keys::APIKeys::Custom(std::move(self), std::move(admins), std::move(users));
        k.encryptionKeys = std::move(encryption_keys);
        return k;
    }
};

void to_json(nlohmann::json& j, const Keys& k);
void from_json(const nlohmann::json& j, Keys& k);

}
