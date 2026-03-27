#pragma once

#include "TestCase.hpp"

#include <vector>
#include <string>
#include <memory>

namespace vh::test::integration {
    struct TestStage {
        std::string name;
        std::vector<std::shared_ptr<TestCase>> tests;
        std::vector<uint32_t> uids{}, gids{};
    };
}
