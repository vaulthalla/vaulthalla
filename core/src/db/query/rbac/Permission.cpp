#include "db/query/rbac/Permission.hpp"
#include "db/Transactions.hpp"
#include "rbac/permission/Permission.hpp"

using namespace vh::db::model;

namespace vh::db::query::rbac {

std::shared_ptr<vh::rbac::permission::Permission> Permission::getPermission(const unsigned int id) {
    return Transactions::exec("Permission::getPermission", [&](pqxx::work& txn) {
        return std::make_shared<vh::rbac::permission::Permission>(txn.exec("SELECT * FROM permission WHERE id = " + txn.quote(id)).one_row());
    });
}

std::shared_ptr<vh::rbac::permission::Permission> Permission::getPermissionByName(const std::string& name) {
    return Transactions::exec("Permission::getPermissionByName", [&](pqxx::work& txn) {
        return std::make_shared<vh::rbac::permission::Permission>(txn.exec("SELECT * FROM permission WHERE name = " + txn.quote(name)).one_row());
    });
}

std::vector<std::shared_ptr<vh::rbac::permission::Permission>> Permission::listPermissions() {
    return Transactions::exec("Permission::listPermissions", [&](pqxx::work& txn) {
        const auto res = txn.exec("SELECT * FROM permission");
        std::vector<std::shared_ptr<vh::rbac::permission::Permission>> out;
        for (const auto& row : res) out.push_back(std::make_shared<vh::rbac::permission::Permission>(row));
        return out;
    });
}

unsigned short Permission::countPermissions() {
    return Transactions::exec("Permission::countPermissions", [&](pqxx::work& txn) {
        return txn.exec("SELECT COUNT(*) FROM permission")[0][0].as<unsigned short>();
    });
}

}
