#pragma once

#include "config/Config.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace vh::config {

class Registry {
public:
    static void init();
    static const Config& get();
    static void set(const Config& config);
    // Deprecated config.yaml keys seen by init() (see loadConfig); the daemon logs them once logging is up.
    static const std::vector<std::string>& deprecations();

private:
    static void ensureInitialized();

    static inline Config config_;
    static inline std::vector<std::string> deprecations_;
    static inline bool initialized_ = false;
    static inline std::once_flag init_flag_;
};

} // namespace vh::config
