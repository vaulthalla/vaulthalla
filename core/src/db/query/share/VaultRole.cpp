#include "db/query/share/VaultRole.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/bytea.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/role/Vault.hpp"

#include <pqxx/pqxx>

#include <stdexcept>
#include <string_view>

namespace vh::db::query::share {
namespace {

void requireShareId(const std::string& shareId) {
    if (shareId.empty()) throw std::invalid_argument("Share id is required");
}

uint32_t resolveRoleTemplateId(
    pqxx::work& txn,
    const std::shared_ptr<vh::rbac::role::Vault>& role
) {
    if (!role) throw std::invalid_argument("Share vault role assignment requires a role template");
    if (role->id != 0) return role->id;
    if (role->name.empty()) throw std::invalid_argument("Share vault role assignment requires a persisted role template");

    const auto res = txn.exec(
        pqxx::prepped{"vault_role_get_by_name"},
        pqxx::params{role->name}
    );
    if (res.empty())
        throw std::runtime_error("Share vault role template is not persisted: " + role->name);
    return res.one_row()["id"].as<uint32_t>();
}

uint32_t upsertAssignmentRow(pqxx::work& txn, const VaultRole::AssignmentInput& input) {
    requireShareId(input.share_id);
    if (input.vault_id == 0) throw std::invalid_argument("Share vault id is required");
    if (input.vault_role_id == 0) throw std::invalid_argument("Share vault role template id is required");
    if (input.subject_type != "public" && input.subject_type != "actor")
        throw std::invalid_argument("Invalid share role assignment subject type");
    if (input.subject_type == "actor" && input.subject_id == 0)
        throw std::invalid_argument("Share actor assignment subject id is required");

    const auto res = txn.exec(
        pqxx::prepped{"share_vault_role_assignment_upsert"},
        pqxx::params{
            input.share_id,
            input.vault_id,
            input.vault_role_id,
            input.subject_type,
            input.subject_id
        }
    );
    if (res.empty()) throw std::runtime_error("Failed to persist share vault role assignment");
    return res.one_row()["id"].as<uint32_t>();
}

uint32_t permissionIdForOverride(pqxx::work& txn, const vh::rbac::permission::Override& override) {
    auto permissionId = override.permission.id;
    if (permissionId != 0) return permissionId;
    if (override.permission.qualified_name.empty())
        throw std::runtime_error("Share vault role override permission id is required");

    const auto permRes = txn.exec(
        pqxx::prepped{"share_vault_role_permission_id_by_name"},
        pqxx::params{override.permission.qualified_name}
    );
    if (permRes.empty())
        throw std::runtime_error("Share vault role override permission is not registered: " +
                                 override.permission.qualified_name);
    return permRes.one_row()["id"].as<uint32_t>();
}

void replaceAssignmentOverrides(
    pqxx::work& txn,
    const uint32_t assignmentId,
    const std::vector<vh::rbac::permission::Override>& overrides
) {
    txn.exec(
        pqxx::prepped{"share_vault_role_assignment_delete_overrides"},
        pqxx::params{assignmentId}
    );

    for (const auto& override : overrides) {
        txn.exec(
            pqxx::prepped{"share_vault_role_assignment_override_insert"},
            pqxx::params{
                assignmentId,
                permissionIdForOverride(txn, override),
                override.glob_path(),
                override.enabled,
                vh::rbac::permission::to_string(override.effect)
            }
        );
    }
}

std::shared_ptr<vh::rbac::role::Vault> getAssignmentWithPrepared(
    pqxx::work& txn,
    const std::string_view prepared,
    const pqxx::params& params
) {
    const auto roleRes = txn.exec(pqxx::prepped{std::string{prepared}}, params);
    if (roleRes.empty()) return nullptr;

    const auto assignmentId = roleRes.one_row()["assignment_id"].as<uint32_t>();
    const auto overrideRes = txn.exec(
        pqxx::prepped{"share_vault_role_assignment_override_list_by_assignment_id"},
        pqxx::params{assignmentId}
    );

    return std::make_shared<vh::rbac::role::Vault>(roleRes.one_row(), overrideRes);
}

}

uint32_t VaultRole::upsertForShare(
    const std::string& shareId,
    const uint32_t vaultId,
    const std::shared_ptr<vh::rbac::role::Vault>& role
) {
    requireShareId(shareId);
    if (vaultId == 0) throw std::invalid_argument("Share vault id is required");

    return Transactions::exec("share::VaultRole::upsertForShare", [&](pqxx::work& txn) {
        const auto roleId = resolveRoleTemplateId(txn, role);
        const auto assignmentId = upsertAssignmentRow(txn, {
            .share_id = shareId,
            .vault_id = vaultId,
            .vault_role_id = roleId,
            .subject_type = "public",
            .subject_id = 0,
            .overrides = role ? role->fs.overrides : std::vector<vh::rbac::permission::Override>{}
        });
        replaceAssignmentOverrides(txn, assignmentId, role->fs.overrides);
        return assignmentId;
    });
}

uint32_t VaultRole::upsertAssignment(const AssignmentInput& input) {
    return Transactions::exec("share::VaultRole::upsertAssignment", [&](pqxx::work& txn) {
        const auto assignmentId = upsertAssignmentRow(txn, input);
        replaceAssignmentOverrides(txn, assignmentId, input.overrides);
        return assignmentId;
    });
}

uint32_t VaultRole::upsertPublicAssignment(
    const std::string& shareId,
    const uint32_t vaultId,
    const uint32_t vaultRoleId,
    const std::vector<vh::rbac::permission::Override>& overrides
) {
    return upsertAssignment({
        .share_id = shareId,
        .vault_id = vaultId,
        .vault_role_id = vaultRoleId,
        .subject_type = "public",
        .subject_id = 0,
        .overrides = overrides
    });
}

uint32_t VaultRole::upsertRecipient(
    const std::string& shareId,
    const std::vector<uint8_t>& emailHash
) {
    requireShareId(shareId);
    if (emailHash.empty()) throw std::invalid_argument("Share recipient email hash is required");

    return Transactions::exec("share::VaultRole::upsertRecipient", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            pqxx::prepped{"share_validated_recipient_upsert"},
            pqxx::params{shareId, vh::db::encoding::to_hex_bytea(emailHash)}
        );
        if (res.empty()) throw std::runtime_error("Failed to persist share recipient");
        return res.one_row()["id"].as<uint32_t>();
    });
}

uint32_t VaultRole::upsertRecipientAssignment(
    const std::string& shareId,
    const uint32_t vaultId,
    const uint32_t recipientId,
    const uint32_t vaultRoleId,
    const std::vector<vh::rbac::permission::Override>& overrides
) {
    return upsertAssignment({
        .share_id = shareId,
        .vault_id = vaultId,
        .vault_role_id = vaultRoleId,
        .subject_type = "actor",
        .subject_id = recipientId,
        .overrides = overrides
    });
}

std::shared_ptr<vh::rbac::role::Vault> VaultRole::getForShare(const std::string& shareId) {
    requireShareId(shareId);

    return Transactions::exec("share::VaultRole::getForShare", [&](pqxx::work& txn) -> std::shared_ptr<vh::rbac::role::Vault> {
        if (auto canonical = getAssignmentWithPrepared(
            txn,
            "share_vault_role_assignment_get_public",
            pqxx::params{shareId}
        )) return canonical;

        const auto roleRes = txn.exec(
            pqxx::prepped{"share_vault_role_get_by_share_id"},
            pqxx::params{shareId}
        );

        if (roleRes.empty()) return nullptr;

        const auto mappingId = roleRes.one_row()["share_vault_role_id"].as<uint32_t>();
        const auto overrideRes = txn.exec(
            pqxx::prepped{"share_vault_role_override_list_by_mapping_id"},
            pqxx::params{mappingId}
        );

        return std::make_shared<vh::rbac::role::Vault>(roleRes.one_row(), overrideRes);
    });
}

std::shared_ptr<vh::rbac::role::Vault> VaultRole::getPublicAssignment(const std::string& shareId) {
    requireShareId(shareId);

    return Transactions::exec("share::VaultRole::getPublicAssignment", [&](pqxx::work& txn) {
        return getAssignmentWithPrepared(
            txn,
            "share_vault_role_assignment_get_public",
            pqxx::params{shareId}
        );
    });
}

std::shared_ptr<vh::rbac::role::Vault> VaultRole::getRecipientAssignment(
    const std::string& shareId,
    const std::vector<uint8_t>& emailHash
) {
    requireShareId(shareId);
    if (emailHash.empty()) throw std::invalid_argument("Share recipient email hash is required");

    return Transactions::exec("share::VaultRole::getRecipientAssignment", [&](pqxx::work& txn) {
        return getAssignmentWithPrepared(
            txn,
            "share_vault_role_assignment_get_actor_by_email_hash",
            pqxx::params{shareId, vh::db::encoding::to_hex_bytea(emailHash)}
        );
    });
}

void VaultRole::removeForShare(const std::string& shareId) {
    requireShareId(shareId);

    Transactions::exec("share::VaultRole::removeForShare", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"share_vault_role_assignment_delete_by_share_id"},
            pqxx::params{shareId}
        );
        txn.exec(
            pqxx::prepped{"share_vault_role_delete_by_share_id"},
            pqxx::params{shareId}
        );
    });
}

}
