#pragma once

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>
#include "protocols/ws/Fwd.hpp"
#include "share/Fwd.hpp"
#include "storage/Fwd.hpp"

namespace vh::protocols::ws::handler::share {

using json = nlohmann::json;

class PreviewReader {
public:
    virtual ~PreviewReader() = default;
    [[nodiscard]] virtual std::vector<uint8_t> readFile(const vh::share::ResolvedTarget& target) const = 0;
};

using SharePreviewEngineResolver = std::function<std::shared_ptr<vh::storage::Engine>(uint32_t vaultId)>;

// The production reader: the source bytes through Engine::openPlaintextReader (the local ciphertext copy when there
// is one: no S3 GET), refused over the render input cap before anything is read. Engines come from the storage
// manager unless a resolver is given.
[[nodiscard]] std::shared_ptr<PreviewReader> makeDefaultPreviewReader(SharePreviewEngineResolver resolver = {});

class Preview {
public:
    using ManagerFactory = std::function<std::shared_ptr<vh::share::Manager>()>;
    using ResolverFactory = std::function<std::shared_ptr<vh::share::TargetResolver>()>;
    using ReaderFactory = std::function<std::shared_ptr<PreviewReader>()>;

    static json get(const json& payload, const std::shared_ptr<Session>& session);

    static void setManagerFactoryForTesting(ManagerFactory factory);
    static void resetManagerFactoryForTesting();
    static void setResolverFactoryForTesting(ResolverFactory factory);
    static void resetResolverFactoryForTesting();
    static void setReaderFactoryForTesting(ReaderFactory factory);
    static void resetReaderFactoryForTesting();
};

}
