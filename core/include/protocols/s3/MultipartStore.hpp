#pragma once

#include "protocols/s3/ObjectStore.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <cstddef>
#include <string>
#include <vector>

namespace vh::protocols::s3 {

class MultipartStore {
public:
    std::string createUpload(const ResolvedBucket& bucket, const std::string& key,
                             const PutObjectOptions& options) const;
    db::query::s3::MultipartPart uploadPart(const ResolvedBucket& bucket, const std::string& key,
                                            const std::string& uploadId, uint32_t partNumber,
                                            const std::vector<uint8_t>& body) const;
    db::query::s3::MultipartPart uploadPartFromFile(const ResolvedBucket& bucket, const std::string& key,
                                                    const std::string& uploadId, uint32_t partNumber,
                                                    const std::filesystem::path& sourcePath,
                                                    uint64_t sizeBytes) const;
    db::query::s3::ObjectState completeUpload(const ResolvedBucket& bucket, const std::string& key,
                                              const std::string& uploadId,
                                              const std::vector<std::pair<uint32_t, std::string>>& requestedParts) const;
    void abortUpload(const ResolvedBucket& bucket, const std::string& key, const std::string& uploadId) const;
    std::size_t abortExpiredUploads() const;
    std::vector<db::query::s3::MultipartUpload> listUploads(const ResolvedBucket& bucket, const std::string& prefix) const;
    std::vector<db::query::s3::MultipartPart> listParts(const ResolvedBucket& bucket, const std::string& key,
                                                        const std::string& uploadId) const;

    static std::filesystem::path partRoot();

private:
    ObjectStore objects_;
};

}
