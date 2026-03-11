#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Module.hpp"

#include <stdexcept>
#include <cstdint>

namespace vh::rbac::permission::admin {

enum class IdentityPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    Add = 1 << 1,
    Edit = 1 << 2,
    Delete = 1 << 3,
    All = View | Add | Edit | Delete
};

enum class GroupPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    Add = 1 << 1,
    Edit = 1 << 2,
    Delete = 1 << 3,
    AddMember = 1 << 4,
    RemoveMember = 1 << 5,
    All = View | Add | Edit | Delete | AddMember | RemoveMember
};

struct IdentitiesBase : Set<IdentityPermissions, uint8_t> {
    [[nodiscard]] bool canView() const noexcept { return has(IdentityPermissions::View); }
    [[nodiscard]] bool canAdd() const noexcept { return has(IdentityPermissions::Add); }
    [[nodiscard]] bool canEdit() const noexcept { return has(IdentityPermissions::Edit); }
    [[nodiscard]] bool canDelete() const noexcept { return has(IdentityPermissions::Delete); }
};

struct Users final : IdentitiesBase {

};

struct Admins final : IdentitiesBase {

};

struct Groups final : Set<GroupPermissions, uint8_t> {
    [[nodiscard]] bool canView() const noexcept { return has(GroupPermissions::View); }
    [[nodiscard]] bool canAdd() const noexcept { return has(GroupPermissions::Add); }
    [[nodiscard]] bool canEdit() const noexcept { return has(GroupPermissions::Edit); }
    [[nodiscard]] bool canDelete() const noexcept { return has(GroupPermissions::Delete); }
    [[nodiscard]] bool canAddMember() const noexcept { return has(GroupPermissions::AddMember); }
    [[nodiscard]] bool canRemoveMember() const noexcept { return has(GroupPermissions::RemoveMember); }
};

struct Identities final : Module<uint64_t> {
    static constexpr const auto* ModuleName = "Identities";
    enum class Type : uint8_t { Users, Admins, Groups };

    Users users;
    Admins admins;
    Groups groups;

    ~Identities() override = default;
    Identities() = default;
    explicit Identities(const Mask& mask) { fromMask(mask); }

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
    [[nodiscard]] bool canView(const Type type) const noexcept {
        return visit(type, [](const auto& node) { return node.canView(); });
    }

    [[nodiscard]] bool canAdd(const Type type) const noexcept {
        return visit(type, [](const auto& node) { return node.canAdd(); });
    }

    [[nodiscard]] bool canEdit(const Type type) const noexcept {
        return visit(type, [](const auto& node) { return node.canEdit(); });
    }

    [[nodiscard]] bool canDelete(const Type type) const noexcept {
        return visit(type, [](const auto& node) { return node.canDelete(); });
    }

    [[nodiscard]] bool canAddMember(const Type type) const noexcept {
        return visit(type, [](const auto& node) {
            if constexpr (requires { node.canAddMember(); })
                return node.canAddMember();
            else
                return false;
        });
    }

    [[nodiscard]] bool canRemoveMember(const Type type) const noexcept {
        return visit(type, [](const auto& node) {
            if constexpr (requires { node.canRemoveMember(); })
                return node.canRemoveMember();
            else
                return false;
        });
    }
};

}
