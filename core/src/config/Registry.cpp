#include "config/Registry.hpp"

#include <stdexcept>
#include <paths.h>

namespace vh::config {

void Registry::init() {
    std::call_once(init_flag_, [&]() {
        const auto path = paths::getConfigPath();
        try {
            config_ = loadConfig(path);
        } catch (const std::exception& e) {
            throw std::runtime_error("cannot load config " + std::string(path) + ": " + e.what());
        }
        initialized_ = true;
    });
}

const Config& Registry::get() {
    ensureInitialized();
    return config_;
}

void Registry::set(const Config& config) {
    config_ = config;
    initialized_ = true;
}

void Registry::ensureInitialized() {
    if (!initialized_)
        throw std::runtime_error("ConfigRegistry accessed before initialization. Call ConfigRegistry::init() first.");
}

}
