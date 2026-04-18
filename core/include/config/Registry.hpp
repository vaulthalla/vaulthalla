#pragma once

#include "config/Config.hpp"

#include <mutex>

namespace vh::config {

class Registry {
public:
    static void init();
    static const Config& get();

private:
    static void ensureInitialized();

    static inline Config config_;
    static inline bool initialized_ = false;
    static inline std::once_flag init_flag_;
};

} // namespace vh::config
