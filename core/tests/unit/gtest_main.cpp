#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <paths.h>

#include "config/Registry.hpp"
#include "log/Registry.hpp"
#include "preview/derive/Runner.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    try {
        vh::paths::enableTestMode();
        // Converter helpers run from the build tree (owned by the developer, not root); the trust checks have
        // their own tests, which re-enable them.
        vh::preview::derive::Runner::setTrustChecksForTesting(false);
        if (const auto* configPath = std::getenv("VH_PATH_TO_CONFIG")) {
            vh::paths::configPath = configPath;
        }
        vh::config::Registry::init();
        vh::log::Registry::init();

    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Vaulthalla test environment: " << e.what() << std::endl;
        return 1;
    }

    return RUN_ALL_TESTS();
}
