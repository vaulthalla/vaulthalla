#pragma once

#include "concurrency/Task.hpp"
#include "fs/Fwd.hpp"
#include "storage/Fwd.hpp"
#include "sync/Fwd.hpp"

#include <memory>

namespace vh::sync::tasks {

struct Upload final : concurrency::PromisedTask {
    std::shared_ptr<storage::CloudEngine> engine;
    std::shared_ptr<fs::model::File> file;
    std::shared_ptr<model::ScopedOp> op;

    Upload(std::shared_ptr<storage::CloudEngine> eng,
                 std::shared_ptr<fs::model::File> f,
                 std::shared_ptr<model::ScopedOp> op);

    void operator()() override;
};

}
