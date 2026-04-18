#pragma once

#include <memory>
#include <string>
#include <variant>
#include <cstdint>

namespace vh::vault::model {
    struct APIKey;
    struct S3Vault;
}

namespace vh::identities {
    struct User;
}

namespace vh::rbac::role {
    struct Vault;
    struct Admin;
}

namespace vh::sync::model {
    struct Waiver {
        struct RoleContext {
            uint32_t id;
            std::string name;
            std::string type; // admin or vault
        };

        unsigned int id{};
        std::shared_ptr<vault::model::S3Vault> vault;
        std::shared_ptr<identities::User> user, owner;
        std::shared_ptr<vault::model::APIKey> apiKey;
        std::variant<std::shared_ptr<rbac::role::Vault>, std::shared_ptr<rbac::role::Admin> > overridingRole;
        bool encrypt_upstream{};
        std::string waiver_text;

        RoleContext resolveOverridingRole() const;
    };
}