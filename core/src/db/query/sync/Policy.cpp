#include "db/query/sync/Policy.hpp"
#include "db/Transactions.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/LocalPolicy.hpp"

namespace vh::db::query::sync {

Policy::PolicyPtr Policy::getSync(const unsigned int vaultId) {
    return Transactions::exec("Policy::getProxySyncConfig", [&](pqxx::work& txn) -> PolicyPtr {
        const auto type = txn.exec("SELECT type FROM vault WHERE id = " + txn.quote(vaultId)).one_field().as<std::string>();

        if (type == "local")
            return std::make_shared<vh::sync::model::LocalPolicy>(
                txn.exec(pqxx::prepped{"get_fsync_config"}, vaultId).one_row());

        if (type == "s3")
            return std::make_shared<vh::sync::model::RemotePolicy>(
                txn.exec(pqxx::prepped{"get_rsync_config"}, vaultId).one_row());

        throw std::runtime_error("Unknown sync type: " + type);
    });
}

void Policy::reportSyncStarted(const unsigned int syncId) {
    Transactions::exec("Policy::reportSyncStarted", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"report_sync_started"}, syncId);
    });
}

void Policy::reportSyncSuccess(const unsigned int syncId) {
    Transactions::exec("Policy::reportSyncSuccess", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"report_sync_success"}, syncId);
    });
}

}
