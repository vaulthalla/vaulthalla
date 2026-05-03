#include "protocols/ws/handler/share/Filesystem.hpp"

#include "protocols/ws/handler/fs/Storage.hpp"

#include <utility>

namespace vh::protocols::ws::handler::share {

json Filesystem::metadata(const json& payload, const std::shared_ptr<Session>& session) {
    return fs::Storage::shareCompatibilityMetadata(payload, session);
}

json Filesystem::list(const json& payload, const std::shared_ptr<Session>& session) {
    return fs::Storage::shareCompatibilityList(payload, session);
}

void Filesystem::setManagerFactoryForTesting(ManagerFactory factory) {
    fs::Storage::setShareManagerFactoryForTesting(std::move(factory));
}

void Filesystem::resetManagerFactoryForTesting() {
    fs::Storage::resetShareManagerFactoryForTesting();
}

void Filesystem::setResolverFactoryForTesting(ResolverFactory factory) {
    fs::Storage::setShareResolverFactoryForTesting(std::move(factory));
}

void Filesystem::resetResolverFactoryForTesting() {
    fs::Storage::resetShareResolverFactoryForTesting();
}

}
