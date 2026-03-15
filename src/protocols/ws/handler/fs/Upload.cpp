#include "protocols/ws/handler/fs/Upload.hpp"
#include "protocols/ws/Session.hpp"
#include "identities/User.hpp"
#include "log/Registry.hpp"
#include "fs/Filesystem.hpp"

#include <filesystem>

using namespace vh::storage;
using namespace vh::identities;
using namespace vh::fs;

namespace vh::protocols::ws::handler::fs {

    Upload::Upload(const std::shared_ptr<Session>& session) : session_(session) {}

    void Upload::startUpload(const UploadArgs& args) {
        log::Registry::ws()->debug("[UploadHandler] Starting upload (uploadId: {}, tmpPath: {}, finalPath: {}, "
                                "fuseFrom: {}, fuseTo: {}, expectedSize: {})",
                                 args.uploadId, args.tmpPath.string(), args.finalPath.string(),
                                 args.fuseFrom.string(), args.fuseTo.string(), args.expectedSize);

        if (currentUpload_) throw std::runtime_error("Upload already in progress");

        if (std::filesystem::is_directory(args.finalPath))
            throw std::runtime_error("Upload final path is a directory — filename must be provided");

        Filesystem::mkdir(args.fuseFrom.parent_path(), 0755, session_->user->id, args.engine);

        currentUpload_.emplace(args);
    }

    void Upload::handleBinaryFrame(beast::flat_buffer& buffer) {
        if (!currentUpload_) throw std::runtime_error("No upload in progress");

        auto& upload = *currentUpload_;
        const auto data = static_cast<const char*>(buffer.data().data());
        const auto size = buffer.size();

        upload.file.write(data, static_cast<long>(size));
        if (!upload.file) throw std::runtime_error("Write error during upload");

        upload.bytesReceived += size;
        buffer.consume(size);
    }

    void Upload::finishUpload() {
        if (!currentUpload_) throw std::runtime_error("No upload in progress");

        auto& upload = *currentUpload_;
        upload.file.close();

        if (upload.bytesReceived != upload.expectedSize) {
            std::filesystem::remove(upload.tmpPath);
            throw std::runtime_error("Upload size mismatch");
        }

        log::Registry::ws()->debug("[UploadHandler] Finishing upload (uploadId: {}, fuseFrom: {}, fuseTo: {}, userId: {})",
                                 upload.uploadId, upload.fuseFrom.string(), upload.fuseTo.string(), session_->user->id);

        Filesystem::rename(upload.fuseFrom, upload.fuseTo, session_->user->id, upload.engine);

        currentUpload_.reset();
    }

}
