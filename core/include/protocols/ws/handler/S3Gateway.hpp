#pragma once

#include "nlohmann/json_fwd.hpp"

#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct S3Gateway {
    static json status(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsCreate(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsList(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsRevoke(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsScopeUpdate(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsScopeList(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsRolesList(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsRolesAssign(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsRolesRevoke(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsRoleOverridesList(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsRoleOverridesAdd(const json& payload, const std::shared_ptr<Session>& session);
    static json credentialsRoleOverridesRemove(const json& payload, const std::shared_ptr<Session>& session);
    static json bucketsList(const json& payload, const std::shared_ptr<Session>& session);
    static json bucketsBind(const json& payload, const std::shared_ptr<Session>& session);
    static json bucketsUnbind(const json& payload, const std::shared_ptr<Session>& session);
    static json bucketsCreateLocal(const json& payload, const std::shared_ptr<Session>& session);
    static json bucketsCreateRemoteCache(const json& payload, const std::shared_ptr<Session>& session);
    static json budgetPolicyList(const json& payload, const std::shared_ptr<Session>& session);
    static json budgetPolicyUpsert(const json& payload, const std::shared_ptr<Session>& session);
    static json budgetPolicyDisable(const json& payload, const std::shared_ptr<Session>& session);
    static json budgetLedgerList(const json& payload, const std::shared_ptr<Session>& session);
    static json budgetStatus(const json& payload, const std::shared_ptr<Session>& session);
};

} // namespace vh::protocols::ws::handler
