#pragma once

#include "concurrency/Task.hpp"
#include "config/Registry.hpp"
#include "preview/thumbnail/ops.hpp"
#include "storage/Engine.hpp"
#include "fs/cache/Record.hpp"
#include "db/query/fs/Cache.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "vault/model/Vault.hpp"
#include "log/Registry.hpp"
#include "runtime/Deps.hpp"
#include "stats/model/CacheStats.hpp"

#include <memory>
#include <string>
#include <filesystem>
#include <chrono>

using namespace vh::vault::model;
using namespace vh::stats::model;
using namespace std::chrono;
using namespace vh::fs;
using namespace vh::fs::model;

namespace vh::preview::thumbnail::task {

class Generate final : public concurrency::Task {
public:
    Generate(const std::shared_ptr<const storage::Engine>& engine,
                  const std::vector<uint8_t>& buffer,
                  const std::shared_ptr<File>& file)
        : engine_(engine), buffer_(buffer), file_(file) {}

    void operator()() override {
        namespace fs = std::filesystem;

        try {
            const auto& sizes = config::Registry::get().caching.thumbnails.sizes;
            const auto basePath = engine_->paths->thumbnailRoot / file_->base32_alias;
            for (const auto& size : sizes) {
                const auto filename = std::to_string(size) + ".jpg";
                fs::path cachePath = basePath / filename;

                if (!fs::exists(cachePath.parent_path())) fs::create_directories(cachePath.parent_path());

                if (!file_->mime_type || file_->mime_type->empty()) {
                    log::Registry::thumb()->warn("[ThumbnailTask] No MIME type for file ID {}. Skipping thumbnail generation.", file_->id);
                    return;
                }

                const auto now = steady_clock::now();
                generateAndStore(buffer_, cachePath, *file_->mime_type, size);
                const auto end = steady_clock::now();
                runtime::Deps::get().httpCacheStats->record_op_us(duration_cast<microseconds>(end - now).count());

                auto index = std::make_shared<cache::Record>();
                index->vault_id = engine_->vault->id;
                index->file_id = file_->id;
                index->path = engine_->paths->relPath(cachePath, PathType::CACHE_ROOT);
                index->type = cache::Record::Type::Thumbnail;
                index->size = fs::file_size(cachePath);

                db::query::fs::Cache::upsertCacheIndex(index);

                runtime::Deps::get().httpCacheStats->record_insert(index->size);
            }
        } catch (const std::exception& e) {
            log::Registry::thumb()->error("[ThumbnailTask] Error generating thumbnail for file ID {}: {}", file_->id, e.what());
        } catch (...) {
            log::Registry::thumb()->error("[ThumbnailTask] Unknown error generating thumbnail for file ID {}", file_->id);
        }
    }

private:
    std::shared_ptr<const storage::Engine> engine_;
    std::vector<uint8_t> buffer_;
    std::shared_ptr<File> file_;
};

}
