#pragma once

#include "concurrency/Task.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace vh::storage { struct Engine; class CloudEngine; }

namespace vh::fs::model { struct File; }

namespace vh::sync::model { struct RemotePolicy; }

namespace vh::sync::tasks {

struct RotateKey final : concurrency::PromisedTask {
    std::shared_ptr<storage::Engine> engine;
    std::shared_ptr<storage::CloudEngine> cloud{nullptr};
    std::vector<std::shared_ptr<fs::model::File>> files;
    std::size_t begin{};
    std::size_t end{};

    RotateKey(std::shared_ptr<storage::Engine> eng,
                  const std::vector<std::shared_ptr<fs::model::File>>& f,
                  std::size_t begin_, std::size_t end_);

    void operator()() override;

private:
    using FileSP = std::shared_ptr<fs::model::File>;
    using RemotePolicySP = std::shared_ptr<model::RemotePolicy>;

    [[nodiscard]] bool shouldSkipLocalWriteInCacheMode(const RemotePolicySP& policy, std::size_t ciphertextSize) const;

    [[nodiscard]] std::vector<uint8_t> produceCiphertext(const FileSP& file,
                                           const std::vector<uint8_t>& buffer,
                                           bool bufferIsEncrypted) const;

    void hydrateIvAndVersionForRemoteEncrypted(const FileSP& file) const;

    void maybeWriteLocal(const RemotePolicySP& policy,
                         const FileSP& file,
                         const std::vector<uint8_t>& ciphertext) const;

    void rotateLocalFile(const FileSP& file) const;

    void rotateCloudFile(const RemotePolicySP& remotePolicy,
                         const FileSP& file) const;
};

}
