#pragma once

#include <memory>
#include <vector>

namespace vh::sync {

namespace model {
struct Action;
}

struct Cloud;

class Executor {
public:
    static void run(const std::shared_ptr<Cloud>& ctx, const std::vector<model::Action>& plan);

private:
    static void dispatch(const std::shared_ptr<Cloud>& ctx, const model::Action& action);
};

}
