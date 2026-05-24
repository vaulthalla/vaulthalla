#pragma once

#include "storage/Engine.hpp"
#include "sync/model/Action.hpp"

#include <unordered_map>
#include <memory>
#include <vector>

namespace vh::vault::model {
    struct S3Vault;
    struct APIKey;
}

namespace vh::sync::model {
    struct RemotePolicy;
}

namespace vh::fs::model {
    struct File;
    struct Directory;

    namespace file {
        struct Trashed;
    }
}

namespace vh::storage {
    namespace s3 {
        class Controller;
        struct S3RequestBudget;
        struct S3RequestMetrics;
    }

    class CloudEngine final : public Engine {
    public:
        CloudEngine() = default;

        ~CloudEngine() override = default;

        explicit CloudEngine(const std::shared_ptr<vault::model::S3Vault> &vault);
        explicit CloudEngine(const std::shared_ptr<vault::model::S3Vault> &vault,
                             std::shared_ptr<s3::Controller> s3Provider);

        [[nodiscard]] StorageType type() const override { return StorageType::Cloud; }

        void purge(const std::filesystem::path &rel_path) const;

        void purge(const std::shared_ptr<vh::fs::model::file::Trashed> &f) const;

        void removeRemotely(const std::filesystem::path &rel_path, bool rmThumbnails = true) const;

        void removeRemotely(const std::shared_ptr<vh::fs::model::file::Trashed> &f, bool rmThumbnails = true) const;

        void upload(const std::shared_ptr<vh::fs::model::File> &f) const;

        void upload(const std::shared_ptr<vh::fs::model::File> &f, const std::vector<uint8_t> &buffer,
                    bool isCiphertext = true) const;

        std::shared_ptr<vh::fs::model::File> downloadFile(const std::filesystem::path &rel_path);

        std::vector<uint8_t> downloadToBuffer(const std::filesystem::path &rel_path) const;

        void indexAndDeleteFile(const std::shared_ptr<vh::fs::model::File> &remoteFile);

        [[nodiscard]] std::string getRemoteContentHash(const std::filesystem::path &rel_path) const;

        [[nodiscard]] std::unordered_map<std::u8string, std::shared_ptr<vh::fs::model::File> > getGroupedFilesFromS3(
            const std::filesystem::path &prefix = {}) const;

        [[nodiscard]] bool refreshRemoteIndexFromManifestIfChanged() const;
        void publishRemoteIndexManifest() const;
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

        void setS3ControllerForTesting(std::shared_ptr<s3::Controller> s3Provider);

    private:
        std::shared_ptr<vault::model::APIKey> key_;
        std::shared_ptr<s3::Controller> s3Provider_;

        std::shared_ptr<vault::model::S3Vault> s3Vault() const;

        std::unordered_map<std::string, std::string> getMetaMapFromFile(
            const std::shared_ptr<vh::fs::model::File> &f) const;
    };
} // namespace vh::storage
