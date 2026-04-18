#include "sync/tasks/RotateKey.hpp"

#include "vault/EncryptionManager.hpp"
#include "db/query/fs/File.hpp"
#include "storage/Engine.hpp"
#include "storage/CloudEngine.hpp"
#include "fs/model/File.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "fs/ops/file.hpp"
#include "log/Registry.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

using namespace vh::sync::tasks;
using namespace vh::storage;
using namespace vh::fs::model;
using namespace vh::fs::ops;

RotateKey::RotateKey(std::shared_ptr<Engine> eng,
                             const std::vector<std::shared_ptr<File>>& f,
                             const std::size_t begin_,
                             const std::size_t end_)
    : engine(std::move(eng)), files(f), begin(begin_), end(end_) {
    if (!engine) throw std::invalid_argument("RotateKeyTask: engine is null");
    if (begin >= end || end > files.size()) throw std::invalid_argument("RotateKeyTask: invalid range");
}

bool RotateKey::shouldSkipLocalWriteInCacheMode(const RemotePolicySP& policy,
                                                    const std::size_t ciphertextSize) const {
    if (!policy) return false;
    if (policy->strategy != model::RemotePolicy::Strategy::Cache) return false;
    return (ciphertextSize * 2) < engine->freeSpace();
}

std::vector<uint8_t> RotateKey::produceCiphertext(const FileSP& file,
                                                      const std::vector<uint8_t>& buffer,
                                                      const bool bufferIsEncrypted) const {
    if (!engine->encryptionManager) throw std::runtime_error("RotateKeyTask: encryptionManager is null");
    if (buffer.empty()) return {};
    return bufferIsEncrypted
        ? engine->encryptionManager->rotateDecryptEncrypt(buffer, file)
        : engine->encryptionManager->encrypt(buffer, file);
}

void RotateKey::hydrateIvAndVersionForRemoteEncrypted(const FileSP& file) const {
    auto payload = cloud->getRemoteIVBase64AndVersion(file->path);
    if (!payload) payload = db::query::fs::File::getEncryptionIVAndVersion(*file->vault_id, file->path);
    if (!payload)
        throw std::runtime_error("RotateKeyTask: no IV/version for encrypted remote file: " + file->backing_path.string());

    const auto& [iv_b64, key_version] = *payload;
    file->encryption_iv = iv_b64;
    file->encrypted_with_key_version = key_version;
}

void RotateKey::maybeWriteLocal(const RemotePolicySP& policy,
                                   const FileSP& file,
                                   const std::vector<uint8_t>& ciphertext) const {
    if (ciphertext.empty()) return;
    if (shouldSkipLocalWriteInCacheMode(policy, ciphertext.size())) return;
    writeFile(file->backing_path, ciphertext);
}

void RotateKey::rotateLocalFile(const FileSP& file) const {
    const auto encryptedBuffer = readFileToVector(file->backing_path);
    const auto ciphertext = engine->encryptionManager->rotateDecryptEncrypt(encryptedBuffer, file);

    if (ciphertext.empty())
        throw std::runtime_error("RotateKeyTask: failed to rotate key for file: " + file->backing_path.string());

    writeFile(file->backing_path, ciphertext);
    db::query::fs::File::setEncryptionIVAndVersion(file);
}

void RotateKey::rotateCloudFile(const RemotePolicySP& remotePolicy,
                                    const FileSP& file) const {
    std::vector<uint8_t> source;
    bool sourceIsEncrypted = false;

    if (!std::filesystem::exists(file->backing_path)) {
        source = cloud->downloadToBuffer(file->path);
        if (source.empty())
            throw std::runtime_error("RotateKeyTask: failed to download file: " + file->backing_path.string());

        sourceIsEncrypted = cloud->remoteFileIsEncrypted(file->path);
        if (sourceIsEncrypted) hydrateIvAndVersionForRemoteEncrypted(file);
    } else {
        source = readFileToVector(file->backing_path);
        if (source.empty()) {
            log::Registry::sync()->warn("[RotateKeyTask] Empty file buffer for: {}", file->backing_path.string());
            return;
        }
        sourceIsEncrypted = true;
    }

    const auto ciphertext = produceCiphertext(file, source, sourceIsEncrypted);
    if (ciphertext.empty())
        throw std::runtime_error("RotateKeyTask: failed to produce ciphertext for: " + file->backing_path.string());

    cloud->upload(file, ciphertext);
    db::query::fs::File::setEncryptionIVAndVersion(file);
    maybeWriteLocal(remotePolicy, file, ciphertext);
}

void RotateKey::operator()() {
    try {
        const auto remotePolicy = std::dynamic_pointer_cast<model::RemotePolicy>(engine->sync);

        if (engine->type() == StorageType::Cloud) {
            cloud = std::static_pointer_cast<CloudEngine>(engine);
            if (!cloud) throw std::runtime_error("RotateKeyTask: failed to cast to CloudStorageEngine");
        }

        for (std::size_t i = begin; i < end; ++i) {
            const auto& file = files[i];
            if (!file || !file->vault_id) continue;

            if (cloud) rotateCloudFile(remotePolicy, file);
            else rotateLocalFile(file);
        }

        promise.set_value(true);
    } catch (const std::exception& e) {
        log::Registry::sync()->error("[RotateKeyTask] Exception during key rotation: {}", e.what());
        promise.set_value(false);
    }
}
