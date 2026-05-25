#pragma once

#include "helpers.hpp"

#include <memory>

namespace vh::fs::model { struct File; }

namespace vh::sync::model {

enum class ActionType {
    EnsureDirectories,
    Upload,
    Download,
    IndexRemoteOnly,
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

struct S3CostEstimate {
    uint64_t list_requests{};
    uint64_t head_requests{};
    uint64_t get_requests{};
    uint64_t put_requests{};
    uint64_t copy_requests{};
    uint64_t delete_requests{};
    uint64_t planned_body_download_bytes{};
    uint64_t planned_upload_bytes{};
    uint64_t remote_index_objects{};
    uint64_t archive_tier_downloads_skipped{};
};

}
