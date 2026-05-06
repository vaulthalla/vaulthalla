#pragma once

#include "sync/model/Operation.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace vh::db::query::sync {

class Operation {
    using O = vh::sync::model::Operation;
    using OperationPtr = std::shared_ptr<O>;

public:
    static void addOperation(const OperationPtr& op);
    static unsigned int createOperation(const OperationPtr& op);
    static void deleteOperation(unsigned int id);
    static std::vector<OperationPtr> listOperationsByVault(unsigned int vaultId);
    static void markOperationCompleted(const OperationPtr& op);
    static void markOperationStatus(unsigned int id, vh::sync::model::Operation::Status status, const std::optional<std::string>& error = std::nullopt);
};

}
