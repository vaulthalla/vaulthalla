#include "db/query/sync/Operation.hpp"
#include "db/Transactions.hpp"
#include "sync/model/Operation.hpp"

using namespace vh::db::query::sync;

void Operation::addOperation(const OperationPtr& op) {
    Transactions::exec("Operation::addOperation", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(op->fs_entry_id);
        p.append(op->executed_by);
        p.append(to_string(op->operation));
        p.append(to_string(op->target));
        p.append(to_string(op->status));
        p.append(op->source_path);
        p.append(op->destination_path);

        txn.exec(pqxx::prepped{"insert_operation"}, p);
    });
}

void Operation::deleteOperation(unsigned int id) {
    Transactions::exec("Operation::deleteOperation", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"delete_operation"}, id);
    });
}

std::vector<Operation::OperationPtr> Operation::listOperationsByVault(unsigned int vaultId) {
    return Transactions::exec("Operation::listOperationsByVault", [&](pqxx::work& txn) {
        pqxx::params p{vaultId};
        const auto res = txn.exec(pqxx::prepped{"list_pending_operations_by_vault"}, p);
        return vh::sync::model::operations_from_pq_res(res);
    });
}

void Operation::markOperationCompleted(const OperationPtr& op) {
    Transactions::exec("Operation::markOperationCompleted", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(op->id);
        p.append(to_string(op->status));
        p.append(op->error.value_or(""));

        txn.exec(pqxx::prepped{"mark_operation_completed_and_update"}, p);
    });
}
