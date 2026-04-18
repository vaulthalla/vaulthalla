#pragma once

#include "sync/model/Action.hpp"

#include <vector>
#include <memory>

namespace vh::sync {

namespace model { struct RemotePolicy; }

struct Cloud;

struct Planner {
    static std::vector<model::Action> build(const std::shared_ptr<Cloud>& ctx, const std::shared_ptr<model::RemotePolicy>& policy);
};

}
