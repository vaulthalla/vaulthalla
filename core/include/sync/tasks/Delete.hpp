#pragma once

#include "concurrency/Task.hpp"
#include "fs/Fwd.hpp"
#include "storage/Fwd.hpp"
#include "sync/Fwd.hpp"

#include <memory>
#include <variant>

namespace vh::sync::tasks {

struct Delete final : concurrency::PromisedTask {
    enum class Type { PURGE, LOCAL, REMOTE };

    using Target = std::variant<
        std::shared_ptr<fs::model::File>,
        std::shared_ptr<fs::model::file::Trashed>
    >;

    std::shared_ptr<storage::Engine> engine;
    Target target;
    std::shared_ptr<model::ScopedOp> op;
    Type type{Type::PURGE};

    Delete(std::shared_ptr<storage::Engine> eng,
               Target tgt,
               std::shared_ptr<model::ScopedOp> op,
               Type type = Type::PURGE);

    void operator()() override;
};

}
