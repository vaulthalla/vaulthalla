#pragma once

#include "curl/wrappers.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <unordered_map>

namespace vh::vault::model { struct APIKey; }

namespace vh::storage::s3 {

namespace fs = std::filesystem;

struct ValidateResult { bool ok; std::string msg; };

class Controller {
public:
    static constexpr uintmax_t MIN_PART_SIZE = 5 * 1024 * 1024; // 5 MiB

    Controller(const std::shared_ptr<vault::model::APIKey>& apiKey, std::string bucket);

    ~Controller();

    // #########################################################################
    // ########################### FILE OPS ####################################
    // #########################################################################

    void uploadLargeObject(const fs::path& key, const fs::path& filePath, uintmax_t partSize = MIN_PART_SIZE) const;
    void uploadObject(const fs::path& key, const fs::path& filePath) const;
    void downloadObject(const fs::path& key, const fs::path& outputPath) const;

    // #########################################################################
    // ########################### BUFFER OPS ##################################
    // #########################################################################

    void uploadLargeObject(const fs::path& key, const std::vector<uint8_t>& buffer, uintmax_t partSize = MIN_PART_SIZE) const;

    void uploadBufferWithMetadata(
        const fs::path& key,
        const std::vector<uint8_t>& buffer,
        const std::unordered_map<std::string, std::string>& metadata) const;

    void downloadToBuffer(const fs::path& key, std::vector<uint8_t>& outBuffer) const;

    // #########################################################################
    // ######################## MULTIPART UPLOADS ##############################
    // #########################################################################

    [[nodiscard]] std::string initiateMultipartUpload(const fs::path& key) const;

    void uploadPart(const fs::path& key, const std::string& uploadId,
                    int partNumber, const std::string& partData, std::string& etagOut) const;

    void completeMultipartUpload(const fs::path& key, const std::string& uploadId,
                                 const std::vector<std::string>& etags) const;

    void abortMultipartUpload(const fs::path& key, const std::string& uploadId) const;

    // #########################################################################
    // ######################## METADATA & TAGS ################################
    // #########################################################################

    [[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> getHeadObject(const fs::path& key) const;
    void setObjectContentHash(const fs::path& key, const std::string& hash) const;
    void setObjectEncryptionMetadata(const std::string& key, const std::string& iv_b64, unsigned int key_version) const;

    // #########################################################################
    // ########################### VALIDATION ##################################
    // #########################################################################

    [[nodiscard]] ValidateResult validateAPICredentials() const;
    [[nodiscard]] bool isBucketEmpty() const;

    // #########################################################################
    // ############################### GENERAL #################################
    // #########################################################################

    void deleteObject(const fs::path& key) const;
    [[nodiscard]] std::u8string listObjects(const fs::path& prefix = {}) const;

private:
    std::shared_ptr<vault::model::APIKey> apiKey_;
    std::string bucket_;

    [[nodiscard]] std::map<std::string, std::string> buildHeaderMap(const std::string& payloadHash) const;

    std::pair<std::string, std::string> constructPaths(CURL* curl, const fs::path& p,
                                                       const std::string& query = "") const;

    [[nodiscard]] SList makeSigHeaders(const std::string& method,
                                       const std::string& canonical,
                                       const std::string& payloadHash,
                                       const std::string& query = "") const;
};

}