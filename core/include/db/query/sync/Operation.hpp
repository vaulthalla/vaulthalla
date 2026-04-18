#pragma once

#include <memory>
#include <vector>

namespace vh::sync::model { struct Operation; }

namespace vh::db::query::sync {

class Operation {
    using O = vh::sync::model::Operation;
    using OperationPtr = std::shared_ptr<O>;

public:
    static void addOperation(const OperationPtr& op);
    static void deleteOperation(unsigned int id);
    static std::vector<OperationPtr> listOperationsByVault(unsigned int vaultId);
    static void markOperationCompleted(const OperationPtr& op);
};

}
