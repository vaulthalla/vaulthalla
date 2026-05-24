#pragma once

#include "curl/wrappers.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <unordered_map>

namespace vh::vault::model {
    struct APIKey;
}

namespace vh::storage::s3 {
    namespace fs = std::filesystem;

    struct ValidateResult {
        bool ok;
        std::string msg;
    };

    struct S3RequestMetrics {
        uint64_t list_requests{};
        uint64_t head_requests{};
        uint64_t get_requests{};
        uint64_t put_requests{};
        uint64_t copy_requests{};
        uint64_t delete_requests{};
        uint64_t downloaded_bytes{};
        bool budget_exceeded{};
        std::string budget_exceeded_reason;
    };

    struct S3RequestBudget {
        std::optional<uint64_t> max_list_requests;
        std::optional<uint64_t> max_head_requests;
        std::optional<uint64_t> max_get_requests;
        std::optional<uint64_t> max_put_requests;
        std::optional<uint64_t> max_copy_requests;
        std::optional<uint64_t> max_delete_requests;
        std::optional<uint64_t> max_downloaded_bytes;
    };

    class RequestBudgetExceeded final : public std::runtime_error {
    public:
        explicit RequestBudgetExceeded(const std::string& message) : std::runtime_error(message) {}
    };

    class Controller {
    public:
        static constexpr uintmax_t MIN_PART_SIZE = 5 * 1024 * 1024; // 5 MiB

        Controller(const std::shared_ptr<vault::model::APIKey> &apiKey, std::string bucket);

        virtual ~Controller();

        void setRequestBudget(const S3RequestBudget& budget) const;
        void clearRequestBudget() const;
        void resetRequestMetrics() const;
        [[nodiscard]] S3RequestMetrics requestMetrics() const;

        // #########################################################################
        // ########################### FILE OPS ####################################
        // #########################################################################

        virtual void uploadLargeObject(const fs::path &key, const fs::path &filePath,
                                       uintmax_t partSize = MIN_PART_SIZE,
                                       const std::unordered_map<std::string, std::string> &metadata = {}) const;

        virtual void uploadObject(const fs::path &key, const fs::path &filePath) const;

        virtual void uploadObjectWithMetadata(
            const fs::path &key,
            const fs::path &filePath,
            const std::unordered_map<std::string, std::string> &metadata) const;

        virtual void downloadObject(const fs::path &key, const fs::path &outputPath) const;

        // #########################################################################
        // ########################### BUFFER OPS ##################################
        // #########################################################################

        virtual void uploadLargeObject(const fs::path &key, const std::vector<uint8_t> &buffer,
                                       uintmax_t partSize = MIN_PART_SIZE,
                                       const std::unordered_map<std::string, std::string> &metadata = {}) const;

        virtual void uploadBufferWithMetadata(
            const fs::path &key,
            const std::vector<uint8_t> &buffer,
            const std::unordered_map<std::string, std::string> &metadata) const;

        virtual void downloadToBuffer(const fs::path &key, std::vector<uint8_t> &outBuffer) const;

        // #########################################################################
        // ######################## MULTIPART UPLOADS ##############################
        // #########################################################################

        [[nodiscard]] virtual std::string initiateMultipartUpload(
            const fs::path &key,
            const std::unordered_map<std::string, std::string> &metadata = {}) const;

        virtual void uploadPart(const fs::path &key, const std::string &uploadId,
                        int partNumber, const std::string &partData, std::string &etagOut) const;

        virtual void completeMultipartUpload(const fs::path &key, const std::string &uploadId,
                                     const std::vector<std::string> &etags) const;

        virtual void abortMultipartUpload(const fs::path &key, const std::string &uploadId) const;

        // #########################################################################
        // ######################## METADATA & TAGS ################################
        // #########################################################################

        [[nodiscard]] virtual std::optional<std::unordered_map<std::string, std::string> > getHeadObject(
            const fs::path &key) const;

        virtual void setObjectContentHash(const fs::path &key, const std::string &hash) const;

        virtual void setObjectEncryptionMetadata(const std::string &key, const std::string &iv_b64,
                                         unsigned int key_version) const;

        // #########################################################################
        // ########################### VALIDATION ##################################
        // #########################################################################

        [[nodiscard]] virtual ValidateResult validateAPICredentials() const;

        [[nodiscard]] virtual bool isBucketEmpty() const;

        // #########################################################################
        // ############################### GENERAL #################################
        // #########################################################################

        virtual void deleteObject(const fs::path &key) const;

        [[nodiscard]] virtual std::u8string listObjects(const fs::path &prefix = {}) const;

    private:
        enum class RequestKind { List, Head, Get, Put, Copy, Delete, DownloadBytes };

        std::shared_ptr<vault::model::APIKey> apiKey_;
        std::string bucket_;
        mutable std::mutex metricsMutex_;
        mutable S3RequestMetrics metrics_;
        mutable std::optional<S3RequestBudget> requestBudget_;

        [[nodiscard]] std::map<std::string, std::string> buildHeaderMap(const std::string &payloadHash) const;

        std::pair<std::string, std::string> constructPaths(CURL *curl, const fs::path &p,
                                                           const std::string &query = "") const;

        [[nodiscard]] SList makeSigHeaders(const std::string &method,
                                           const std::string &canonical,
                                           const std::string &payloadHash,
                                           const std::string &query = "") const;

        void recordRequest(RequestKind kind, uint64_t amount = 1) const;
    };
}
