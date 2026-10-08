#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>
#include "protocols/ws/Fwd.hpp"
#include "share/Fwd.hpp"
#include "storage/Fwd.hpp"

namespace vh::protocols::ws::handler::share {

using json = nlohmann::json;

class DownloadReader {
public:
    virtual ~DownloadReader() = default;
    [[nodiscard]] virtual std::vector<uint8_t> readFile(const vh::share::ResolvedTarget& target) const = 0;
};

using ShareEngineResolver = std::function<std::shared_ptr<vh::storage::Engine>(uint32_t vaultId)>;

// The production reader: the file's bytes through Engine::openPlaintextReader (the local ciphertext copy when there
// is one, so a cloud file with a local copy costs no S3 GET; a remote-only file follows preview.media.remote).
// Engines come from the storage manager unless a resolver is given.
[[nodiscard]] std::shared_ptr<DownloadReader> makeDefaultDownloadReader(ShareEngineResolver resolver = {});

class Download {
public:
    using ManagerFactory = std::function<std::shared_ptr<vh::share::Manager>()>;
    using ResolverFactory = std::function<std::shared_ptr<vh::share::TargetResolver>()>;
    using ReaderFactory = std::function<std::shared_ptr<DownloadReader>()>;

    static json start(const json& payload, const std::shared_ptr<Session>& session);
    static json chunk(const json& payload, const std::shared_ptr<Session>& session);
    static json cancel(const json& payload, const std::shared_ptr<Session>& session);
    static json nativeStart(const json& payload, const std::shared_ptr<Session>& session);
    static json nativeChunk(const json& payload, const std::shared_ptr<Session>& session);
    static json nativeCancel(const json& payload, const std::shared_ptr<Session>& session);

    static void setManagerFactoryForTesting(ManagerFactory factory);
    static void resetManagerFactoryForTesting();
    static void setResolverFactoryForTesting(ResolverFactory factory);
    static void resetResolverFactoryForTesting();
    static void setReaderFactoryForTesting(ReaderFactory factory);
    static void resetReaderFactoryForTesting();
    static void resetTransfersForTesting();
};

}
