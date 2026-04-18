#include "sync/tasks/Download.hpp"
#include "storage/CloudEngine.hpp"
#include "fs/model/File.hpp"
#include "log/Registry.hpp"
#include "sync/model/ScopedOp.hpp"

using namespace vh::sync::tasks;
using namespace vh::storage;
using namespace vh::fs::model;

Download::Download(std::shared_ptr<CloudEngine> eng,
                           std::shared_ptr<File> f,
                           model::ScopedOp& op,
                           const bool freeAfter)
    : engine(std::move(eng)), file(std::move(f)), op(op), freeAfterDownload(freeAfter) {}

void Download::operator()() {
    try {
        op.start(file->size_bytes);
        if (freeAfterDownload) engine->indexAndDeleteFile(file->path);
        else engine->downloadFile(file->path);
        op.success = true;
    } catch (const std::exception& e) {
        log::Registry::sync()->error("[DownloadTask] Failed to download file: {} - {}", file->path.string(), e.what());
    }

    op.stop();
    promise.set_value(op.success);
}
