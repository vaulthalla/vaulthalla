#pragma once

#include "concurrency/Task.hpp"
#include "fs/Fwd.hpp"
#include "sync/rotation/Rotation.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace vh::sync::tasks {

// Rotates files[begin, end) on the sync pool (see sync/rotation/Rotation.hpp for the protocol). Every file is
// attempted; `result` says what happened to each, and is complete once the future is ready. The future's value is
// true when no file failed.
struct RotateKey final : concurrency::PromisedTask {
    using Files = std::vector<std::shared_ptr<fs::model::File>>;

    std::shared_ptr<const rotation::Deps> deps;
    std::shared_ptr<const Files> files;
    std::size_t begin{};
    std::size_t end{};
    rotation::BatchResult result;

    RotateKey(std::shared_ptr<const rotation::Deps> deps_,
              std::shared_ptr<const Files> files_,
              std::size_t begin_, std::size_t end_);

    void operator()() override;
};

}
