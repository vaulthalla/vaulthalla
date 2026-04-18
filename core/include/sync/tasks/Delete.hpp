#pragma once

#include "concurrency/Task.hpp"

#include <memory>
#include <variant>

namespace vh::storage {
struct Engine;
class CloudEngine;
}

namespace vh::fs::model {
struct File;
namespace file { struct Trashed; }
}

namespace vh::sync::model {
struct ScopedOp;
}

namespace vh::sync::tasks {

struct Delete final : concurrency::PromisedTask {
    enum class Type { PURGE, LOCAL, REMOTE };

    using Target = std::variant<
        std::shared_ptr<fs::model::File>,
        std::shared_ptr<fs::model::file::Trashed>
    >;

    std::shared_ptr<storage::Engine> engine;
    Target target;
    model::ScopedOp& op;
    Type type{Type::PURGE};

    Delete(std::shared_ptr<storage::Engine> eng,
               Target tgt,
               model::ScopedOp& op,
               Type type = Type::PURGE);

    void operator()() override;
};

}
