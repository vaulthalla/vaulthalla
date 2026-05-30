#pragma once

#include "nlohmann/json_fwd.hpp"

#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct Pricing {
    static json policyList(const json& payload, const std::shared_ptr<Session>& session);
    static json policyUpsert(const json& payload, const std::shared_ptr<Session>& session);
    static json policyDisable(const json& payload, const std::shared_ptr<Session>& session);
    static json ledgerList(const json& payload, const std::shared_ptr<Session>& session);
    static json status(const json& payload, const std::shared_ptr<Session>& session);
    static json preflight(const json& payload, const std::shared_ptr<Session>& session);
    static json overrideRequest(const json& payload, const std::shared_ptr<Session>& session);
    static json overrideApprove(const json& payload, const std::shared_ptr<Session>& session);
    static json overrideDeny(const json& payload, const std::shared_ptr<Session>& session);
    static json overrideList(const json& payload, const std::shared_ptr<Session>& session);
    static json notificationsList(const json& payload, const std::shared_ptr<Session>& session);
    static json notificationsAck(const json& payload, const std::shared_ptr<Session>& session);
};

}
