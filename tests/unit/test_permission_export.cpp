#include "rbac/role/Vault.hpp"
#include "rbac/role/Admin.hpp"

#include <gtest/gtest.h>
#include <iostream>

using namespace vh;

class PermExportTest : public ::testing::Test {
protected:
    void SetUp() override { /* no-op for now */ }
};

TEST_F(PermExportTest, TestVaultPermExport) {
    const auto vRole = rbac::role::Vault::PowerUser();
    EXPECT_FALSE(vRole.toFlagsString().empty());
    // std::cout << vRole.toFlagsString() << std::endl;

    const auto flags = vRole.getFlags();
    EXPECT_FALSE(flags.empty());
    for (auto& perm : flags)
        std::cout << perm << std::endl;
}

TEST_F(PermExportTest, TestAdminPermExport) {
    const auto role = rbac::role::Admin::SecurityAdmin();
    EXPECT_FALSE(role.toFlagsString().empty());

    const auto flags = role.getFlags();
    EXPECT_FALSE(flags.empty());
    for (auto& perm : flags)
        std::cout << perm << std::endl;
}
