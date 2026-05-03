#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <fstream>
#include <optional>
#include <memory>
#include <stdexcept>
#include <vector>
#include <boost/beast/core/flat_buffer.hpp>

namespace vh::storage { struct Engine; }
namespace vh::fs::model { struct File; }
namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler::fs {

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

struct ShareUploadArgs {
    std::string uploadId;
    std::string shareId;
    std::string shareSessionId;
    std::string websocketSessionUuid;
    uint64_t expectedSize = 0;
    uint64_t maxChunkSize = 256u * 1024u;
    uint64_t maxTransferSize = 64u * 1024u * 1024u;

    std::function<void(uint64_t)> onChunk;
    std::function<std::shared_ptr<vh::fs::model::File>(const std::vector<uint8_t>&)> onFinish;
    std::function<void()> onCancel;
    std::function<void(const std::string&)> onFail;
};

struct ShareUploadContext : ShareUploadArgs {
    uint64_t bytesReceived = 0;
    std::vector<uint8_t> buffer;

    explicit ShareUploadContext(const ShareUploadArgs& args)
        : ShareUploadArgs(args) {
        if (expectedSize > maxTransferSize)
            throw std::runtime_error("Share upload exceeds maximum transfer size");
        buffer.reserve(static_cast<size_t>(expectedSize));
    }
};

class Upload {
public:
    explicit Upload(const std::shared_ptr<Session>& session);

    void startUpload(const UploadArgs& args);
    void startShareUpload(const ShareUploadArgs& args);

    void handleBinaryFrame(boost::beast::flat_buffer& buffer);
    void abortActiveUpload(const std::string& reason = "upload_aborted") noexcept;

    void finishUpload();
    [[nodiscard]] std::shared_ptr<vh::fs::model::File> finishShareUpload(
        const std::string& uploadId,
        const std::string& shareSessionId
    );
    void cancelShareUpload(const std::string& uploadId, const std::string& shareSessionId);

    [[nodiscard]] bool uploadInProgress() const { return currentUpload_.has_value() || currentShareUpload_.has_value(); }
    [[nodiscard]] bool shareUploadInProgress() const { return currentShareUpload_.has_value(); }
    [[nodiscard]] std::optional<std::string> activeShareUploadId() const;

private:
    void abortHumanUpload(const std::string& reason) noexcept;
    void abortShareUpload(const std::string& reason) noexcept;

    std::weak_ptr<Session> session_;
    std::optional<UploadContext> currentUpload_;
    std::optional<ShareUploadContext> currentShareUpload_;
};

}
