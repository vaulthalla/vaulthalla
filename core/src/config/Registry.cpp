#include "config/Registry.hpp"

#include <stdexcept>
#include <paths.h>

namespace vh::config {

void Registry::init() {
    std::call_once(init_flag_, [&]() {
        config_ = loadConfig(paths::getConfigPath());
        initialized_ = true;
    });
}

const Config& Registry::get() {
    ensureInitialized();
    return config_;
}

void Registry::ensureInitialized() {
    if (!initialized_)
        throw std::runtime_error("ConfigRegistry accessed before initialization. Call ConfigRegistry::init() first.");
}

}
