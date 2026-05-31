#include "protocols/s3/MultipartStore.hpp"

#include "config/Registry.hpp"
#include "db/query/s3/Gateway.hpp"
#include "identities/User.hpp"
#include "protocols/s3/Error.hpp"

#include <array>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <openssl/md5.h>
#include <paths.h>
#include <sstream>

namespace vh::protocols::s3 {

namespace {
void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

uint64_t appendFileToStream(const std::filesystem::path& path, std::ofstream& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw invalidArgument("Unable to read multipart part file", path.string());

    uint64_t total = 0;
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count <= 0) break;
        out.write(buffer.data(), count);
        if (!out) throw invalidArgument("Unable to write completed multipart temp file", path.string());
        total += static_cast<uint64_t>(count);
    }
    return total;
}

std::string multipartHex(const unsigned char* bytes, const std::size_t n) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < n; ++i)
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    return out.str();
}

std::vector<uint8_t> bytesFromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

std::string md5FileHex(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw invalidArgument("Unable to read multipart part file", path.string());

    MD5_CTX ctx{};
    MD5_Init(&ctx);
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) MD5_Update(&ctx, buffer.data(), static_cast<std::size_t>(count));
    }
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    return multipartHex(digest, MD5_DIGEST_LENGTH);
}

std::string objectKeyFor(const std::string& key) {
    return ObjectStore::vaultPathToKey(ObjectStore::keyToVaultPath(key));
}

db::query::s3::MultipartUpload requireUploadFor(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::string& uploadId) {
    const auto upload = db::query::s3::Gateway::getMultipartUpload(uploadId);
    if (!upload || upload->vault_id != bucket.vault_id || upload->object_key != objectKeyFor(key))
        throw noSuchUpload(uploadId);
    return *upload;
}
}

std::filesystem::path MultipartStore::partRoot() {
    const auto& configured = config::Registry::get().s3_gateway.multipart.part_dir;
    if (!configured.empty()) return configured;
    return paths::getBackingPath() / "s3-multipart";
}

std::string MultipartStore::createUpload(
    const ResolvedBucket& bucket,
    const std::string& key,
    const PutObjectOptions& options) const {
    (void)abortExpiredUploads();

    const auto uploadId = boost::uuids::to_string(boost::uuids::random_generator()());
    db::query::s3::Gateway::createMultipartUpload({
        .upload_id = uploadId,
        .vault_id = bucket.vault_id,
        .object_key = ObjectStore::vaultPathToKey(ObjectStore::keyToVaultPath(key)),
        .initiated_by = bucket.actor ? bucket.actor->id : 0,
        .content_type = options.content_type,
        .metadata = options.metadata,
        .storage_class = options.storage_class
    });
    return uploadId;
}

db::query::s3::MultipartPart MultipartStore::uploadPart(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::string& uploadId,
    const uint32_t partNumber,
    const std::vector<uint8_t>& body) const {
    if (partNumber == 0 || partNumber > 10000)
        throw invalidArgument("Part number must be between 1 and 10000");
    (void)requireUploadFor(bucket, key, uploadId);

    const auto md5Hex = ObjectStore::md5Hex(body);
    const auto path = partRoot() / uploadId / std::to_string(partNumber);
    writeFile(path, body);

    db::query::s3::MultipartPart part{
        .upload_id = uploadId,
        .part_number = partNumber,
        .etag = "\"" + md5Hex + "\"",
        .size_bytes = body.size(),
        .md5 = bytesFromHex(md5Hex),
        .path = path,
        .created_at = std::time(nullptr)
    };
    db::query::s3::Gateway::upsertMultipartPart(part);
    return *db::query::s3::Gateway::getMultipartPart(uploadId, partNumber);
}

db::query::s3::MultipartPart MultipartStore::uploadPartFromFile(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::string& uploadId,
    const uint32_t partNumber,
    const std::filesystem::path& sourcePath,
    const uint64_t sizeBytes) const {
    if (partNumber == 0 || partNumber > 10000)
        throw invalidArgument("Part number must be between 1 and 10000");
    (void)requireUploadFor(bucket, key, uploadId);

    const auto md5Hex = md5FileHex(sourcePath);
    const auto path = partRoot() / uploadId / std::to_string(partNumber);
    std::filesystem::create_directories(path.parent_path());
    std::error_code ec;
    std::filesystem::rename(sourcePath, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy_file(sourcePath, path, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) throw invalidArgument("Unable to store multipart part file", sourcePath.string());
    }

    db::query::s3::MultipartPart part{
        .upload_id = uploadId,
        .part_number = partNumber,
        .etag = "\"" + md5Hex + "\"",
        .size_bytes = sizeBytes,
        .md5 = bytesFromHex(md5Hex),
        .path = path,
        .created_at = std::time(nullptr)
    };
    db::query::s3::Gateway::upsertMultipartPart(part);
    return *db::query::s3::Gateway::getMultipartPart(uploadId, partNumber);
}

db::query::s3::ObjectState MultipartStore::completeUpload(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::string& uploadId,
    const std::vector<std::pair<uint32_t, std::string>>& requestedParts) const {
    const auto upload = requireUploadFor(bucket, key, uploadId);
    auto parts = db::query::s3::Gateway::listMultipartParts(uploadId);
    if (parts.empty()) throw invalidArgument("Multipart upload has no parts", key);

    std::vector<std::vector<uint8_t>> partMd5s;

    const auto useRequested = !requestedParts.empty();
    if (useRequested) {
        parts.clear();
        uint32_t previousPartNumber = 0;
        for (const auto& [partNumber, etag] : requestedParts) {
            if (partNumber <= previousPartNumber)
                throw S3Error{"InvalidPartOrder", "Multipart parts must be listed in ascending order",
                              http::status::bad_request, key};
            previousPartNumber = partNumber;

            auto part = db::query::s3::Gateway::getMultipartPart(uploadId, partNumber);
            if (!part) throw invalidArgument("Missing multipart part " + std::to_string(partNumber), key);
            if (!etag.empty() && part->etag != etag && part->etag != "\"" + etag + "\"")
                throw invalidArgument("Multipart part ETag mismatch", key);
            parts.push_back(*part);
        }
    }

    const auto minPartSize = static_cast<uint64_t>(config::Registry::get().s3_gateway.multipart.min_part_size_mb) *
                             1024ull * 1024ull;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i].size_bytes < minPartSize)
            throw S3Error{"EntityTooSmall", "Multipart parts must be at least the configured minimum size",
                          http::status::bad_request, key};
    }

    const auto completePath = partRoot() / uploadId / ".complete-object";
    std::filesystem::create_directories(completePath.parent_path());
    std::ofstream complete(completePath, std::ios::binary | std::ios::trunc);
    if (!complete) throw invalidArgument("Unable to create completed multipart temp file", key);

    uint64_t totalSize = 0;
    for (const auto& part : parts) {
        totalSize += appendFileToStream(part.path, complete);
        partMd5s.push_back(part.md5);
    }
    complete.close();

    PutObjectOptions options{
        .content_type = upload.content_type.value_or("application/octet-stream"),
        .storage_class = upload.storage_class,
        .metadata = upload.metadata,
        .multipart = true,
        .part_count = static_cast<uint32_t>(parts.size()),
        .etag_override = ObjectStore::multipartEtag(partMd5s)
    };

    auto state = objects_.putObjectFromFile(bucket, key, completePath, totalSize, options);
    db::query::s3::Gateway::markMultipartUploadCompleted(uploadId);
    for (const auto& part : parts) std::filesystem::remove(part.path);
    db::query::s3::Gateway::deleteMultipartParts(uploadId);
    std::filesystem::remove_all(partRoot() / uploadId);
    return state;
}

void MultipartStore::abortUpload(const ResolvedBucket& bucket, const std::string& key, const std::string& uploadId) const {
    (void)requireUploadFor(bucket, key, uploadId);
    const auto parts = db::query::s3::Gateway::listMultipartParts(uploadId);
    for (const auto& part : parts) std::filesystem::remove(part.path);
    db::query::s3::Gateway::deleteMultipartParts(uploadId);
    db::query::s3::Gateway::abortMultipartUpload(uploadId);
    std::filesystem::remove_all(partRoot() / uploadId);
}

std::size_t MultipartStore::abortExpiredUploads() const {
    const auto days = config::Registry::get().s3_gateway.multipart.abort_after_days;
    const auto cutoff = std::time(nullptr) - static_cast<std::time_t>(days) * 24 * 60 * 60;
    const auto uploads = db::query::s3::Gateway::listMultipartUploadsInitiatedBefore(cutoff);

    std::size_t aborted = 0;
    for (const auto& upload : uploads) {
        const auto parts = db::query::s3::Gateway::listMultipartParts(upload.upload_id);
        for (const auto& part : parts) std::filesystem::remove(part.path);
        db::query::s3::Gateway::deleteMultipartParts(upload.upload_id);
        db::query::s3::Gateway::abortMultipartUpload(upload.upload_id);
        std::filesystem::remove_all(partRoot() / upload.upload_id);
        ++aborted;
    }
    return aborted;
}

std::vector<db::query::s3::MultipartUpload> MultipartStore::listUploads(
    const ResolvedBucket& bucket,
    const std::string& prefix) const {
    (void)abortExpiredUploads();
    return db::query::s3::Gateway::listMultipartUploads(bucket.vault_id, prefix);
}

std::vector<db::query::s3::MultipartPart> MultipartStore::listParts(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::string& uploadId) const {
    (void)requireUploadFor(bucket, key, uploadId);
    return db::query::s3::Gateway::listMultipartParts(uploadId);
}

} // namespace vh::protocols::s3
