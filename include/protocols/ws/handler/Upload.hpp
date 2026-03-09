#pragma once

#include <filesystem>
#include <string>
#include <fstream>
#include <optional>
#include <boost/beast/core/flat_buffer.hpp>

namespace vh::storage { struct Engine; }
namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler {

struct UploadArgs {
    std::string uploadId;
    uint64_t expectedSize = 0;
    std::shared_ptr<storage::Engine> engine;
    std::filesystem::path tmpPath, finalPath, fuseFrom, fuseTo;
};

struct UploadContext : UploadArgs {
    uint64_t bytesReceived = 0;
    std::ofstream file;

    explicit UploadContext(const UploadArgs& args)
        : UploadArgs(args),
          file(args.tmpPath, std::ios::binary | std::ios::trunc) {
        if (!file.is_open()) throw std::runtime_error("Cannot open temp file");
    }
};

class Upload {
public:
    explicit Upload(const std::shared_ptr<Session>& session);

    void startUpload(const UploadArgs& args);

    void handleBinaryFrame(boost::beast::flat_buffer& buffer);

    void finishUpload();

    [[nodiscard]] bool uploadInProgress() const { return currentUpload_.has_value(); }

private:
    const std::shared_ptr<Session>& session_;
    std::optional<UploadContext> currentUpload_;
};

}
