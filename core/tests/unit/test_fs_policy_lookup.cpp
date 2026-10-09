// A file Lookup (seeing it, in FUSE lookups and filtered listings) is implied by being allowed to download it (#183).
// Before every FUSE lookup reached the daemon, a download-only grant only worked while another uid's walk had left the
// dentries cached; uncached, the file was hidden from the very user allowed to read it.

#include "rbac/fs/glob/model/Pattern.hpp"
#include "rbac/fs/policy/Evaluator.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/role/Vault.hpp"

#include <gtest/gtest.h>

#include <string>

namespace vh::test_fs_policy_lookup {

using Action = rbac::permission::vault::FilesystemAction;
using rbac::fs::policy::Evaluator;

rbac::permission::Override fileOverride(const std::string& permission, const std::string& glob,
                                        const rbac::permission::OverrideOpt effect) {
    rbac::permission::Override out;
    out.permission.qualified_name = permission;
    out.effect = effect;
    out.enabled = true;
    out.pattern = rbac::fs::glob::model::Pattern::make(glob);
    return out;
}

bool allowed(const rbac::permission::vault::Filesystem& perms, const Action action, const std::string& path,
             const bool isDirectory = false) {
    return Evaluator::evaluate(perms, {.action = action, .vaultPath = path, .exists = true, .isDirectory = isDirectory})
        .allowed;
}

TEST(FsPolicyLookup, ADownloadGrantMakesTheFileVisible) {
    auto perms = rbac::role::Vault::ImplicitDeny().fs;
    perms.overrides.push_back(
        fileOverride("vault.fs.files.download", "/seed/docs/*.txt", rbac::permission::OverrideOpt::ALLOW));

    EXPECT_TRUE(allowed(perms, Action::Read, "/seed/docs/secret.txt"));
    EXPECT_TRUE(allowed(perms, Action::Lookup, "/seed/docs/secret.txt")) << "readable but hidden";
    // Nothing else opens up: other files stay hidden, and Lookup grants no preview.
    EXPECT_FALSE(allowed(perms, Action::Lookup, "/seed/note.txt"));
    EXPECT_FALSE(allowed(perms, Action::Preview, "/seed/docs/secret.txt"));
}

TEST(FsPolicyLookup, ADeniedDownloadDoesNotRevealTheFile) {
    auto perms = rbac::role::Vault::ImplicitDeny().fs;
    perms.overrides.push_back(
        fileOverride("vault.fs.files.download", "/seed/docs/*.txt", rbac::permission::OverrideOpt::DENY));

    EXPECT_FALSE(allowed(perms, Action::Read, "/seed/docs/secret.txt"));
    EXPECT_FALSE(allowed(perms, Action::Lookup, "/seed/docs/secret.txt"));
}

}
