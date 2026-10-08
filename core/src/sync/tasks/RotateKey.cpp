#include "sync/tasks/RotateKey.hpp"

#include "log/Registry.hpp"

#include <stdexcept>
#include <utility>

namespace vh::sync::tasks {

RotateKey::RotateKey(std::shared_ptr<const rotation::Deps> deps_,
                     std::shared_ptr<const Files> files_,
                     const std::size_t begin_,
                     const std::size_t end_)
    : deps(std::move(deps_)), files(std::move(files_)), begin(begin_), end(end_) {
    if (!deps) throw std::invalid_argument("RotateKeyTask: rotation dependencies are null");
    if (!files) throw std::invalid_argument("RotateKeyTask: file list is null");
    if (begin >= end || end > files->size()) throw std::invalid_argument("RotateKeyTask: invalid range");
}

void RotateKey::operator()() {
    try {
        result = rotation::rotateRange(*files, begin, end, *deps);
    } catch (const std::exception& e) {
        log::Registry::sync()->error("[RotateKeyTask] Key rotation range failed: {}", e.what());
        result.failures.push_back({{}, e.what()});
    } catch (...) {
        log::Registry::sync()->error("[RotateKeyTask] Key rotation range failed: unknown error");
        result.failures.push_back({{}, "unknown error"});
    }
    promise.set_value(result.failures.empty());
}

}
