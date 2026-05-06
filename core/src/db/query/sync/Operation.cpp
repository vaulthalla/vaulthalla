#include "db/query/sync/Operation.hpp"
#include "db/Transactions.hpp"
#include "sync/model/Operation.hpp"

namespace vh::db::query::sync {

void Operation::addOperation(const OperationPtr& op) {
    (void)createOperation(op);
}

unsigned int Operation::createOperation(const OperationPtr& op) {
    return Transactions::exec("Operation::addOperation", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(op->fs_entry_id);
        p.append(op->executed_by);
        p.append(to_string(op->operation));
        p.append(to_string(op->target));
        p.append(to_string(op->status));
        p.append(op->source_path);
        p.append(op->destination_path);

        op->id = txn.exec(pqxx::prepped{"insert_operation"}, p).one_row()["id"].as<unsigned int>();
        return op->id;
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
    markOperationStatus(op->id, op->status, op->error);
}

void Operation::markOperationStatus(
    const unsigned int id,
    const vh::sync::model::Operation::Status status,
    const std::optional<std::string>& error
) {
    Transactions::exec("Operation::markOperationCompleted", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(id);
        p.append(to_string(status));
        p.append(error);

        txn.exec(pqxx::prepped{"mark_operation_completed_and_update"}, p);
    });
}

}
