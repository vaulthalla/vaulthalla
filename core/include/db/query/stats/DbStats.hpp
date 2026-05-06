#pragma once

#include <memory>

namespace vh::stats::model { struct DbStats; }

namespace vh::db::query::stats {

struct DbStats {
    static std::shared_ptr<vh::stats::model::DbStats> snapshot();
};

}
