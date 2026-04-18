#pragma once

#include "storage/Engine.hpp"
#include "fs/model/File.hpp"
#include "task/Generate.hpp"
#include "concurrency/ThreadPool.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "log/Registry.hpp"

#include <memory>
#include <string>

using namespace vh::concurrency;
using namespace vh::fs::model;

namespace vh::preview::thumbnail {

struct Worker {
    static void enqueue(const std::shared_ptr<storage::Engine>& engine,
             const std::vector<uint8_t>& buffer,
             const std::shared_ptr<File>& file) {
        try {
            if (const std::string& mime = file->mime_type ? *file->mime_type : "unknown";
                !(mime.starts_with("image/") || mime.starts_with("application/"))) return;
            auto task = std::make_unique<task::Generate>(engine, buffer, file);
            ThreadPoolManager::instance().thumbPool()->submit(std::move(task));
        } catch (const std::exception& e) {
            log::Registry::thumb()->error("[ThumbnailWorker] Failed to enqueue thumbnail task: {}", e.what());
        }
    }
};

}
