#pragma once

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace vh::db::model { struct ListQueryParams; }
namespace vh::share { struct Upload; }

namespace vh::db::query::share {

struct Upload {
    static std::shared_ptr<vh::share::Upload> create(const std::shared_ptr<vh::share::Upload>& upload);
    static std::shared_ptr<vh::share::Upload> get(const std::string& upload_id);
    static void addReceivedBytes(const std::string& upload_id, uint64_t bytes);
    static void complete(const std::string& upload_id, uint32_t entry_id, const std::string& content_hash, const std::string& mime_type);
    static void fail(const std::string& upload_id, const std::string& error);
    static void cancel(const std::string& upload_id);
    static uint64_t sumCompletedBytes(const std::string& share_id);
    static uint64_t countCompletedFiles(const std::string& share_id);
    static std::vector<std::shared_ptr<vh::share::Upload>> listForShare(const std::string& share_id, const db::model::ListQueryParams& params);
    static std::vector<std::shared_ptr<vh::share::Upload>> listStaleActive(std::time_t older_than_seconds, uint64_t limit);
};

}
