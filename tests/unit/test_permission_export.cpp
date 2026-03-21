#include "rbac/role/Vault.hpp"

#include <gtest/gtest.h>
#include <iostream>

using namespace vh;

class PermExportTest : public ::testing::Test {
protected:
    void SetUp() override { /* no-op for now */ }
};

TEST_F(PermExportTest, TestVaultPermExport) {
    std::cout << "Testing Vault Permission Export..." << std::endl;

    const auto vRole = rbac::role::Vault::PowerUser();
    std::cout << vRole.toFlagsString() << std::endl;

    for (const auto& perm : vRole.getFlags())
        std::cout << perm << std::endl;
}
