#pragma once

#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/admin/identities/Users.hpp"
#include "rbac/permission/admin/identities/Groups.hpp"
#include "rbac/permission/admin/identities/Admins.hpp"

#include <stdexcept>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin {

struct Identities final : Module<uint64_t> {
    static constexpr const auto* ModuleName = "Identities";
    enum class Type : uint8_t { Users, Admins, Groups };

    identities::Users users;
    identities::Admins admins;
    identities::Groups groups;

    ~Identities() override = default;
    Identities() = default;
    explicit Identities(const Mask& mask) { fromMask(mask); }

    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] const char* name() const override { return ModuleName; }
    [[nodiscard]] uint64_t toMask() const override { return pack(users, admins, groups); }
    void fromMask(const uint64_t mask) override { unpack(mask, users, admins, groups); }

private:
    template <typename Fn>
    decltype(auto) visit(const Type type, Fn&& fn) {
        switch (type) {
        case Type::Users:  return fn(users);
        case Type::Admins: return fn(admins);
        case Type::Groups: return fn(groups);
        }

        throw std::runtime_error("Identities::visit: invalid type");
    }

    template <typename Fn>
    decltype(auto) visit(const Type type, Fn&& fn) const {
        switch (type) {
        case Type::Users:  return fn(users);
        case Type::Admins: return fn(admins);
        case Type::Groups: return fn(groups);
        }

        throw std::runtime_error("Identities::visit: invalid type");
    }

public:
    [[nodiscard]] bool canView(const Type type) const noexcept;
    [[nodiscard]] bool canAdd(const Type type) const noexcept;
    [[nodiscard]] bool canEdit(const Type type) const noexcept;
    [[nodiscard]] bool canDelete(const Type type) const noexcept;
    [[nodiscard]] bool canAddMember(const Type type) const noexcept;
    [[nodiscard]] bool canRemoveMember(const Type type) const noexcept;
};

void to_json(nlohmann::json& j, const Identities& identities);
void from_json(const nlohmann::json& j, Identities& identities);

}
