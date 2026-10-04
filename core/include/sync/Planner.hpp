#pragma once

#include "sync/model/Action.hpp"
#include "sync/Fwd.hpp"

#include <vector>
#include <memory>

namespace vh::sync {

struct Planner {
    static std::vector<model::Action> build(
        const std::shared_ptr<Cloud>& ctx,
        const std::shared_ptr<model::RemotePolicy>& policy,
        model::S3CostEstimate* planningNotes = nullptr);
    static model::S3CostEstimate estimateS3Cost(const std::vector<model::Action>& plan);
};

}
