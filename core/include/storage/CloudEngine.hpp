#pragma once

#include "storage/Engine.hpp"
#include "storage/RemoteFetch.hpp"
#include "sync/model/Action.hpp"
#include "storage/s3/provider/Provider.hpp"
#include "fs/Fwd.hpp"
#include "sync/Fwd.hpp"
#include "vault/Fwd.hpp"

#include <future>
#include <mutex>
#include <chrono>
#include <exception>
#include <unordered_map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::storage {
    namespace s3 {
        class Controller;
        struct RequestOptions;
        struct S3RequestBudget;
        struct S3RequestMetrics;
    }

    class CloudEngine final : public Engine {
    public:
        struct RemoteEncryptionResolveOptions {
            bool trust_file_encryption_metadata{false};
            bool allow_local_db_recovery{false};

            constexpr RemoteEncryptionResolveOptions() = default;
            constexpr RemoteEncryptionResolveOptions(
                bool trustFileEncryptionMetadata,
                bool allowLocalDbRecovery)
                : trust_file_encryption_metadata(trustFileEncryptionMetadata),
                  allow_local_db_recovery(allowLocalDbRecovery) {}
        };

        CloudEngine() = default;

        ~CloudEngine() override = default;

        explicit CloudEngine(const std::shared_ptr<vault::model::S3Vault> &vault);
        explicit CloudEngine(const std::shared_ptr<vault::model::S3Vault> &vault,
                             std::shared_ptr<s3::Controller> s3Provider);

        [[nodiscard]] StorageType type() const override { return StorageType::Cloud; }

        void purge(const std::filesystem::path &rel_path) const;

        void purge(const std::shared_ptr<vh::fs::model::file::Trashed> &f) const;

        void removeRemotely(const std::filesystem::path &rel_path) const;

        void removeRemotely(const std::shared_ptr<vh::fs::model::file::Trashed> &f) const;

        void upload(const std::shared_ptr<vh::fs::model::File> &f) const;

        void upload(const std::shared_ptr<vh::fs::model::File> &f, const std::vector<uint8_t> &buffer,
                    bool isCiphertext = true) const;

        std::shared_ptr<vh::fs::model::File> uploadBufferObject(
            const std::filesystem::path &rel_path,
            const std::vector<uint8_t> &plaintext,
            std::optional<std::string> contentHash = std::nullopt) const;

        std::shared_ptr<vh::fs::model::File> downloadFile(const std::filesystem::path &rel_path);
        std::shared_ptr<vh::fs::model::File> downloadFile(const std::shared_ptr<vh::fs::model::File> &remoteFile);

        std::vector<uint8_t> downloadToBuffer(const std::filesystem::path &rel_path) const;
        [[nodiscard]] std::vector<uint8_t> decryptRemotePayload(
            const std::filesystem::path &rel_path,
            const std::vector<uint8_t> &payload,
            const std::shared_ptr<vh::fs::model::File> &remoteFile = nullptr) const;

        [[nodiscard]] std::vector<uint8_t> decryptRemotePayload(
            const std::filesystem::path &rel_path,
            const std::vector<uint8_t> &payload,
            const std::shared_ptr<vh::fs::model::File> &remoteFile,
            RemoteEncryptionResolveOptions options) const;

        void indexAndDeleteFile(const std::shared_ptr<vh::fs::model::File> &remoteFile);

        [[nodiscard]] std::string getRemoteContentHash(const std::filesystem::path &rel_path) const;

        [[nodiscard]] std::unordered_map<std::u8string, std::shared_ptr<vh::fs::model::File> > getGroupedFilesFromS3(
            const std::filesystem::path &prefix = {}) const;

        [[nodiscard]] bool refreshRemoteIndexFromManifestIfChanged() const;
        void publishRemoteIndexManifest(const std::optional<std::string>& expectedETag = std::nullopt) const;
        void publishRemoteIndexManifestWithRetry() const;
        void applyRemoteIndexMutation(const std::vector<sync::model::Action>& plan) const;
        [[nodiscard]] bool selectedDownloadRequiresRestore(const std::shared_ptr<vh::fs::model::File>& remoteFile) const;

        std::vector<std::shared_ptr<vh::fs::model::Directory> > extractDirectories(
            const std::vector<std::shared_ptr<vh::fs::model::File> > &files) const;

        [[nodiscard]] bool remoteFileIsEncrypted(const std::filesystem::path &rel_path) const;

        std::optional<std::pair<std::string, unsigned int> > getRemoteIVBase64AndVersion(
            const std::filesystem::path &rel_path) const;

        std::shared_ptr<sync::model::RemotePolicy> remote_policy() const;

        void configureS3RequestBudget(const s3::S3RequestBudget& budget) const;
        void clearS3RequestBudget() const;
        void resetS3RequestMetrics() const;
        [[nodiscard]] s3::S3RequestMetrics s3RequestMetrics() const;
        [[nodiscard]] std::shared_ptr<vault::model::APIKey> s3ApiKey() const { return key_; }
        [[nodiscard]] s3::provider::ProfilePtr s3ProviderProfile() const { return s3Profile_; }
        [[nodiscard]] std::optional<s3::provider::StorageTier> resolvedStorageTier() const { return storageTier_; }

        void setS3ControllerForTesting(std::shared_ptr<s3::Controller> s3Provider);
        void setS3ProviderProfileForTesting(s3::provider::ProfilePtr profile);

        // Fetches a remote-only file's object once and keeps it as the local *ciphertext* copy at f->backing_path
        // (never plaintext on disk). Price-preflighted (RemoteFetchGate) before any body is fetched, metered and
        // capped (one HEAD, one GET, exactly the object's bytes), bound to the HEAD's ETag with If-Match, and
        // authenticated (whole GCM message) before the copy becomes visible. An object stored in plaintext
        // (encrypt_upstream off) is sealed on the fly under a fresh IV, recorded in the files row by
        // compare-and-set before the copy is linked in. Concurrent callers for one file share a single fetch.
        // Returns the file as it now reads locally (IV and key version of the stored copy). Throws ContentUnavailable
        // (refused by a budget, archived, missing or changed remotely, or the row changed meanwhile), IntegrityError
        // (the object failed authentication: nothing is kept), or std::runtime_error/std::system_error.
        // The copy stays local afterwards: the Cache strategy has no eviction yet.
        std::shared_ptr<vh::fs::model::File> hydrate(const std::shared_ptr<vh::fs::model::File> &f) const;

        // Price preflight for remote reads (default: priceBudgetRemoteFetchGate).
        void setRemoteFetchGate(RemoteFetchGate gate);
        // Per-reader request caps for the opt-in ranged reader.
        void setRangedReadLimits(const RangedReadLimits &limits);
        [[nodiscard]] const RangedReadLimits &rangedReadLimits() const { return rangedLimits_; }
        // Test seam for the files-row compare-and-set a hydrate needs when the stored IV changes.
        void setHydrateCatalogCommitForTesting(HydrateCatalogCommit commit);

    protected:
        // Remote-only file: preview.media.remote (ReaderOptions::remote) hydrate (default) | ranged | off.
        [[nodiscard]] std::unique_ptr<PlaintextReader> openMissingReader(
            const std::shared_ptr<vh::fs::model::File> &f, const ReaderOptions &options) const override;

    private:
        // What one HEAD says about a remote object.
        struct RemoteObjectHead {
            std::string etag;
            std::optional<uint64_t> content_length;
            bool encrypted{};
            std::string iv_b64;           // empty when the object carries none (or is plaintext)
            unsigned int key_version{};
            bool requires_restore{};      // archive tier without a completed restore
        };

        [[nodiscard]] std::optional<RemoteObjectHead> headRemoteObject(const std::filesystem::path &rel_path) const;
        [[nodiscard]] std::shared_ptr<vh::fs::model::File> hydrateNow(const std::shared_ptr<vh::fs::model::File> &f) const;
        [[nodiscard]] std::unique_ptr<PlaintextReader> openRangedReader(
            const std::shared_ptr<vh::fs::model::File> &f) const;
        [[nodiscard]] std::unique_ptr<RemoteFetchReservation> reserveRemoteFetch(const RemoteFetchRequest &request) const;

        struct RemoteEncryptionContext {
            bool encrypted{};
            std::optional<std::pair<std::string, unsigned int>> payload;
        };

        std::shared_ptr<vault::model::APIKey> key_;
        std::shared_ptr<s3::Controller> s3Provider_;
        s3::provider::ProfilePtr s3Profile_;
        std::optional<s3::provider::StorageTier> storageTier_;

        RemoteFetchGate fetchGate_;
        HydrateCatalogCommit catalogCommit_;
        RangedReadLimits rangedLimits_;
        // One in-flight hydrate per backing path; waiters share its outcome.
        mutable std::mutex hydrateMutex_;
        mutable std::unordered_map<std::string, std::shared_future<std::shared_ptr<vh::fs::model::File>>> hydrating_;
        // Recent failures (by backing path + row IV): a tampered, mismatched or refused object is not fetched again
        // on every request; it is retried after kHydrateFailureBackoff or as soon as the row changes.
        struct HydrateFailure {
            std::chrono::steady_clock::time_point at;
            std::exception_ptr error;
        };
        mutable std::unordered_map<std::string, HydrateFailure> hydrateFailures_;

        std::shared_ptr<vault::model::S3Vault> s3Vault() const;
        void resolveS3ProviderConfiguration();
        [[nodiscard]] s3::RequestOptions requestOptionsFor(s3::provider::RequestOperation operation) const;
        [[nodiscard]] std::optional<std::string> configuredStorageClass() const;

        std::unordered_map<std::string, std::string> getMetaMapFromFile(
            const std::shared_ptr<vh::fs::model::File> &f) const;

        std::shared_ptr<vh::fs::model::File> downloadFileWithRemoteMetadata(
            const std::filesystem::path &rel_path,
            const std::shared_ptr<vh::fs::model::File> &remoteFile);

        [[nodiscard]] RemoteEncryptionContext resolveRemoteEncryptionContext(
            const std::filesystem::path &rel_path,
            const std::shared_ptr<vh::fs::model::File> &remoteFile,
            RemoteEncryptionResolveOptions options) const;
    };
} // namespace vh::storage
