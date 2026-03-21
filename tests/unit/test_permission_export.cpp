#include "rbac/role/Vault.hpp"

#include <gtest/gtest.h>
#include <iostream>

using namespace vh;

class PermExportTest : public ::testing::Test {
protected:
    void SetUp() override { /* no-op for now */ }
};

TEST_F(PermExportTest, TestVaultPermExport) {
    const auto vRole = rbac::role::Vault::PowerUser();
    if (vRole.toFlagsString().empty()) EXPECT_FALSE(vRole.toFlagsString().empty());
    // std::cout << vRole.toFlagsString() << std::endl;

    EXPECT_FALSE(vRole.getFlags().empty());
    // for (const auto& perm : vRole.getFlags())
    //     std::cout << perm << std::endl;
}
