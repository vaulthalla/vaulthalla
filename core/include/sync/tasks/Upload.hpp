#pragma once

#include "concurrency/Task.hpp"

#include <memory>

namespace vh::storage {
class CloudEngine;
}

namespace vh::fs::model { struct File; }

namespace vh::sync::model {
struct ScopedOp;
}

namespace vh::sync::tasks {

struct Upload final : concurrency::PromisedTask {
    std::shared_ptr<storage::CloudEngine> engine;
    std::shared_ptr<fs::model::File> file;
    model::ScopedOp& op;

    Upload(std::shared_ptr<storage::CloudEngine> eng,
                 std::shared_ptr<fs::model::File> f,
                 model::ScopedOp& op);

    void operator()() override;
};

}
