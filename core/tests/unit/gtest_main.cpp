#include <gtest/gtest.h>
#include <filesystem>
#include <paths.h>

#include "config/Registry.hpp"
#include "log/Registry.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    try {
        vh::paths::setLogPathForTesting();
        vh::config::Registry::init();
        vh::log::Registry::init();

    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Vaulthalla test environment: " << e.what() << std::endl;
        return 1;
    }

    return RUN_ALL_TESTS();
}
