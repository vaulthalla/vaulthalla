#pragma once

#include "helpers.hpp"

#include <memory>

namespace vh::fs::model { struct File; }

namespace vh::sync::model {

enum class ActionType {
    EnsureDirectories,
    Upload,
    Download,
    DeleteLocal,
    DeleteRemote,
};

struct Action {
    ActionType type{ActionType::EnsureDirectories};
    EntryKey key;
    std::shared_ptr<fs::model::File> local{};
    std::shared_ptr<fs::model::File> remote{};
    bool freeAfterDownload = false; // cache mode hint
};

}
