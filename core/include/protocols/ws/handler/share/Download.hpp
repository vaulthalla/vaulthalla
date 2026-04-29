#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace vh::protocols::ws {
class Session;
}

namespace vh::share {
class Manager;
class TargetResolver;
struct ResolvedTarget;
}

namespace vh::protocols::ws::handler::share {

using json = nlohmann::json;

class DownloadReader {
public:
    virtual ~DownloadReader() = default;
    [[nodiscard]] virtual std::vector<uint8_t> readFile(const vh::share::ResolvedTarget& target) const = 0;
};

class Download {
public:
    using ManagerFactory = std::function<std::shared_ptr<vh::share::Manager>()>;
    using ResolverFactory = std::function<std::shared_ptr<vh::share::TargetResolver>()>;
    using ReaderFactory = std::function<std::shared_ptr<DownloadReader>()>;

    static json start(const json& payload, const std::shared_ptr<Session>& session);
    static json chunk(const json& payload, const std::shared_ptr<Session>& session);
    static json cancel(const json& payload, const std::shared_ptr<Session>& session);

    static void setManagerFactoryForTesting(ManagerFactory factory);
    static void resetManagerFactoryForTesting();
    static void setResolverFactoryForTesting(ResolverFactory factory);
    static void resetResolverFactoryForTesting();
    static void setReaderFactoryForTesting(ReaderFactory factory);
    static void resetReaderFactoryForTesting();
    static void resetTransfersForTesting();
};

}
