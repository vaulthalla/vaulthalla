#pragma once

#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/admin/identities/Users.hpp"
#include "rbac/permission/admin/identities/Groups.hpp"
#include "rbac/permission/admin/identities/Admins.hpp"

#include <stdexcept>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin {

struct Identities final : Module<uint32_t> {
    static constexpr const auto* ModuleName = "Identities";
    enum class Type : uint8_t { Users, Admins, Groups };

    identities::Users users{};
    identities::Admins admins{};
    identities::Groups groups{};

    ~Identities() override = default;
    Identities() = default;
    explicit Identities(const Mask& mask) { fromMask(mask); }

    [[nodiscard]] std::string toFlagsString() const override;
    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] const char* name() const override { return ModuleName; }

    [[nodiscard]] std::vector<std::string> getFlags() const override { return Module::getFlags(users, admins, groups); }

    [[nodiscard]] uint32_t toMask() const override { return pack(users, admins, groups); }
    void fromMask(const uint32_t mask) override { unpack(mask, users, admins, groups); }

    [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
        return packAndExportPerms(
            mount("admin.identities.users", users),
            mount("admin.identities.admins", admins),
            mount("admin.identities.groups", groups)
        );
    }

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

    static Identities None() {
        Identities i;
        i.users = identities::Users::None();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::None();
        return i;
    }

    static Identities ViewOnly() {
        Identities i;
        i.users = identities::Users::ViewOnly();
        i.admins = identities::Admins::ViewOnly();
        i.groups = identities::Groups::ViewOnly();
        return i;
    }

    static Identities UserSupport() {
        Identities i;
        i.users = identities::Users::ViewOnly();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::MemberViewer();
        return i;
    }

    static Identities UserManager() {
        Identities i;
        i.users = identities::Users::Manager();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::MemberViewer();
        return i;
    }

    static Identities GroupMemberManager() {
        Identities i;
        i.users = identities::Users::ViewOnly();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::MemberManager();
        return i;
    }

    static Identities GroupManager() {
        Identities i;
        i.users = identities::Users::ViewOnly();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::GroupManager();
        return i;
    }

    static Identities UserAndGroupManager() {
        Identities i;
        i.users = identities::Users::Manager();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::Full();
        return i;
    }

    static Identities PrivilegedIdentityManager() {
        Identities i;
        i.users = identities::Users::Manager();
        i.admins = identities::Admins::Manager();
        i.groups = identities::Groups::Full();
        return i;
    }

    static Identities Users() {
        Identities i;
        i.users = identities::Users::Full();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::None();
        return i;
    }

    static Identities UsersAndGroups() {
        Identities i;
        i.users = identities::Users::Full();
        i.admins = identities::Admins::None();
        i.groups = identities::Groups::Full();
        return i;
    }

    static Identities Full() {
        Identities i;
        i.users = identities::Users::Full();
        i.admins = identities::Admins::Full();
        i.groups = identities::Groups::Full();
        return i;
    }

    static Identities Custom(identities::Users users, identities::Admins admins, identities::Groups groups) {
        Identities i;
        i.users = std::move(users);
        i.admins = std::move(admins);
        i.groups = std::move(groups);
        return i;
    }
};

void to_json(nlohmann::json& j, const Identities& identities);
void from_json(const nlohmann::json& j, Identities& identities);

}
