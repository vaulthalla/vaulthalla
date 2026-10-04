#pragma once

#include <memory>
#include <vector>
#include "sync/Fwd.hpp"

namespace vh::sync {

namespace model {
struct Action;
}

class Executor {
public:
    static void run(const std::shared_ptr<Cloud>& ctx, const std::vector<model::Action>& plan);

private:
    static void dispatch(const std::shared_ptr<Cloud>& ctx, const model::Action& action);
};

}
