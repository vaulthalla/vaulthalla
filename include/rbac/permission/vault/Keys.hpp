#pragma once

#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/vault/APIKey.hpp"
#include "rbac/permission/vault/EncryptionKey.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault {

struct Keys final : Module<uint16_t> {
    static constexpr const auto* ModuleName = "Keys";
    APIKey apiKey;
    EncryptionKey encryptionKey;

    Keys() = default;
    explicit Keys(const Mask& mask) { fromMask(mask); }

    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] const char* name() const override { return ModuleName; }
    [[nodiscard]] uint16_t toMask() const override { return pack(apiKey, encryptionKey); }
    void fromMask(const Mask mask) override { unpack(mask, apiKey, encryptionKey); }
};

void to_json(nlohmann::json& j, const Keys& k);
void from_json(const nlohmann::json& j, Keys& k);

}
