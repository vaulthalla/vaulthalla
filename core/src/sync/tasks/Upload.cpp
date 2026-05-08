#include "sync/tasks/Upload.hpp"
#include "storage/CloudEngine.hpp"
#include "fs/model/File.hpp"
#include "log/Registry.hpp"
#include "sync/model/ScopedOp.hpp"

using namespace vh::sync::tasks;
using namespace vh::storage;
using namespace vh::fs::model;

Upload::Upload(std::shared_ptr<CloudEngine> eng, std::shared_ptr<File> f, std::shared_ptr<model::ScopedOp> op)
    : engine(std::move(eng)), file(std::move(f)), op(std::move(op)) {}

void Upload::operator()() {
    try {
        if (!op) throw std::runtime_error("UploadTask: null scoped operation");
        op->start(file->size_bytes);
        engine->upload(file);
        op->success = true;
    } catch (const std::exception& e) {
        log::Registry::sync()->error("[UploadTask] Failed to upload file: {} - {}", file->path.string(), e.what());
    }

    if (op) op->stop();
    promise.set_value(op && op->success);
}
