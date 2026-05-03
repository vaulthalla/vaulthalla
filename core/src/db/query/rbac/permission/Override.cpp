#include "db/query/rbac/permission/Override.hpp"
#include "db/Transactions.hpp"
#include "db/template/Paginate.hpp"
#include "rbac/permission/Override.hpp"

#include <pqxx/pqxx>
#include <stdexcept>
#include <vector>

namespace vh::db::query::rbac::permission {

using OverrideT = vh::rbac::permission::Override;
using OverridePtr = std::shared_ptr<OverrideT>;

unsigned int Override::upsert(const OverridePtr &permOverride) {
    if (!permOverride) throw std::invalid_argument("permission::Override::upsert received null override");

    return Transactions::exec("permission::Override::upsert", [&](pqxx::work &txn) -> unsigned int {
        pqxx::params p;
        p.append(permOverride->assignment_id);
        p.append(permOverride->permission.id);
        p.append(permOverride->glob_path());
        p.append(permOverride->enabled);
        p.append(to_string(permOverride->effect));

        const auto res = txn.exec(pqxx::prepped{"upsert_vault_permission_override"}, p);
        if (res.empty()) throw std::runtime_error("Failed to upsert vault permission override");

        return res.one_row()[0].as<unsigned int>();
    });
}

unsigned int Override::add(const OverridePtr &permOverride) {
    if (!permOverride) throw std::invalid_argument("permission::Override::add received null override");

    return Transactions::exec("permission::Override::add", [&](pqxx::work &txn) -> unsigned int {
        pqxx::params p;
        p.append(permOverride->assignment_id);
        p.append(permOverride->permission.id);
        p.append(permOverride->glob_path());
        p.append(permOverride->enabled);
        p.append(to_string(permOverride->effect));

        const auto res = txn.exec(pqxx::prepped{"insert_vault_permission_override"}, p);
        if (res.empty()) throw std::runtime_error("Failed to insert vault permission override");

        return res.one_row()[0].as<unsigned int>();
    });
}

void Override::update(const OverridePtr &permOverride) {
    if (!permOverride) throw std::invalid_argument("permission::Override::update received null override");

    Transactions::exec("permission::Override::update", [&](pqxx::work &txn) {
        pqxx::params p;
        p.append(permOverride->id);
        p.append(permOverride->glob_path());
        p.append(permOverride->enabled);
        p.append(to_string(permOverride->effect));

        txn.exec(pqxx::prepped{"update_vault_permission_override"}, p);
    });
}

void Override::remove(unsigned int permOverrideId) {
    Transactions::exec("permission::Override::remove", [&](pqxx::work &txn) {
        txn.exec(pqxx::prepped{"delete_vault_permission_override"}, pqxx::params{permOverrideId});
    });
}

void Override::removeByAssignment(unsigned int assignmentId) {
    Transactions::exec("permission::Override::removeByAssignment", [&](pqxx::work &txn) {
        txn.exec(pqxx::prepped{"delete_vault_permission_overrides_by_assignment"}, pqxx::params{assignmentId});
    });
}

OverridePtr Override::get(unsigned int permOverrideId) {
    return Transactions::exec("permission::Override::get(id)", [&](pqxx::work &txn) -> OverridePtr {
        const auto res = txn.exec(
            pqxx::prepped{"get_vault_permission_override_by_id"},
            pqxx::params{permOverrideId}
        );

        if (res.empty()) return nullptr;
        return std::make_shared<OverrideT>(res.one_row());
    });
}

OverridePtr Override::get(const Query &query) {
    return Transactions::exec("permission::Override::get(query)", [&](pqxx::work &txn) -> OverridePtr {
        pqxx::params p;
        p.append(query.vault_id);
        p.append(query.subject_type);
        p.append(query.subject_id);
        p.append(query.bit_position);

        const auto res = txn.exec(
            pqxx::prepped{"get_permission_override_by_vault_subject_and_bitpos"},
            p
        );

        if (res.empty()) return nullptr;
        return std::make_shared<OverrideT>(res.one_row());
    });
}

bool Override::exists(unsigned int assignmentId, unsigned int permissionId, const std::string &globPath) {
    return Transactions::exec("permission::Override::exists", [&](pqxx::work &txn) -> bool {
        const auto res = txn.exec(
            pqxx::prepped{"exists_vault_permission_override"},
            pqxx::params{assignmentId, permissionId, globPath}
        );

        return !res.empty() && res.one_row()[0].as<bool>();
    });
}

std::vector<OverridePtr> Override::list(unsigned int vaultId, model::ListQueryParams &&params) {
    return Transactions::exec("permission::Override::list(vault)", [&](pqxx::work &txn) -> std::vector<OverridePtr> {
        const auto res = txn.exec(
            pqxx::prepped{"list_vault_permission_overrides"},
            pqxx::params{vaultId}
        );

        std::vector<OverridePtr> overrides;
        overrides.reserve(res.size());

        for (const auto &row: res)
            overrides.emplace_back(std::make_shared<OverrideT>(row));

        return template_::paginate(std::move(overrides), params);
    });
}

std::vector<OverridePtr> Override::listAssigned(unsigned int assignmentId, model::ListQueryParams &&params) {
    return Transactions::exec("permission::Override::listAssigned", [&](pqxx::work &txn) -> std::vector<OverridePtr> {
        const auto res = txn.exec(
            pqxx::prepped{"vault_permission_override_list_by_assignment_id"},
            pqxx::params{assignmentId}
        );

        std::vector<OverridePtr> overrides;
        overrides.reserve(res.size());

        for (const auto &row: res)
            overrides.emplace_back(std::make_shared<OverrideT>(row));

        return template_::paginate(std::move(overrides), params);
    });
}

std::vector<OverridePtr> Override::listForSubject(const std::string &subjectType, unsigned int subjectId,
                                                  model::ListQueryParams &&params) {
    return Transactions::exec("permission::Override::listForSubject", [&](pqxx::work &txn) -> std::vector<OverridePtr> {
        const auto res = txn.exec(
            pqxx::prepped{"list_subject_permission_overrides"},
            pqxx::params{subjectType, subjectId}
        );

        std::vector<OverridePtr> overrides;
        overrides.reserve(res.size());

        for (const auto &row: res)
            overrides.emplace_back(std::make_shared<OverrideT>(row));

        return template_::paginate(std::move(overrides), params);
    });
}

}
