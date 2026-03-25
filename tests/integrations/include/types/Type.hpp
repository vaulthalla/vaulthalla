#pragma once

#include "protocols/shell/types.hpp"

namespace vh::test::integrations {

enum class EntityType {
    USER,
    VAULT,
    GROUP,
    ADMIN_ROLE,
    VAULT_ROLE
};

enum class CommandType {
    CREATE,
    UPDATE,
    DELETE,
    LIST,
    INFO,
    ASSIGN,
    UNASSIGN
};

enum class ActionType {
    ADD,
    REMOVE,
    SET
};

struct EntityResult {
    protocols::shell::CommandResult result{};
    std::shared_ptr<void> entity{};
};

inline std::string EntityTypeToString(const EntityType& type) {
    switch (type) {
    case EntityType::USER: return "user";
    case EntityType::VAULT: return "vault";
    case EntityType::GROUP: return "group";
    case EntityType::ADMIN_ROLE:
    case EntityType::VAULT_ROLE: return "role";
    default: return "unknown";
    }
}

inline std::string CommandTypeToString(const CommandType& type) {
    switch (type) {
    case CommandType::CREATE: return "create";
    case CommandType::UPDATE: return "update";
    case CommandType::DELETE: return "delete";
    case CommandType::LIST: return "list";
    case CommandType::INFO: return "info";
    case CommandType::ASSIGN: return "assign";
    case CommandType::UNASSIGN: return "unassign";
    default: return "unknown";
    }
}

inline std::string ActionTypeToString(const ActionType& type) {
    switch (type) {
    case ActionType::ADD: return "add";
    case ActionType::REMOVE: return "remove";
    case ActionType::SET: return "set";
    default: return "unknown";
    }
}

}
