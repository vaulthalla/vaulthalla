#pragma once

#include "TestCase.hpp"
#include "types/Type.hpp"

#include <cstdint>
#include <memory>
#include <filesystem>
#include <vector>

namespace vh {
    namespace identities { struct User; struct Group; }
    namespace storage { struct Engine; }
    namespace rbac {
        namespace permission { struct Override; enum class OverrideOpt; }
        namespace role { struct Vault; struct Admin; }
    }
}

namespace vh::test::integration::fuse {
    enum class TargetSubject : uint8_t { User, Group };

    struct BuilderSpec {
        std::string name;
        std::filesystem::path baseDir;
    };

    struct FuseStep {
        std::shared_ptr<TestCase> tc;
        std::function<ExecResult()> fn;
    };

    struct FuseSubject {
        std::shared_ptr<identities::User> user;
        std::shared_ptr<identities::Group> group; // optional
        std::shared_ptr<rbac::role::Vault> userVaultRole;  // optional
        std::shared_ptr<rbac::role::Vault> groupVaultRole; // optional
        uid_t uid{};
        gid_t gid{};
    };

    struct FuseScenarioContext {
        std::string name;
        std::shared_ptr<identities::User> admin;
        std::shared_ptr<storage::Engine> engine;
        std::filesystem::path root;
        std::string baseDir;
        std::vector<FuseStep> steps{};

        [[nodiscard]] std::filesystem::path base() const { return root / baseDir; }
        [[nodiscard]] std::filesystem::path docs() const { return base() / "docs"; }
        [[nodiscard]] std::filesystem::path secret() const { return docs() / "secret.txt"; }
        [[nodiscard]] std::filesystem::path note() const { return base() / "note.txt"; }
        [[nodiscard]] std::filesystem::path hello() const { return base() / "hello.txt"; }
    };

    struct FuseTestCaseSpec {
        std::string name;
        std::string path;
        int expect_exit{0};
        std::vector<std::string> must_contain{};
        std::vector<std::string> must_not_contain{};
        std::function<ExecResult()> fn;
    };

    struct UserSpec {
        std::string userNameSeed{"user/create"};
        std::string adminRoleTemplateName{"unprivileged"};
    };

    struct VaultRoleSpec {
        TargetSubject subjectType;
        std::string templateName{"implicit_deny"};                     // "power_user", "implicit_deny"
        std::string roleNameSeed{"vRole/create"};                     // "vault_role/create/override"
        std::string description{"Vault role for testing"};
        std::vector<rbac::permission::Override> overrides{};

        [[nodiscard]] std::string getSubjectType() const {
            switch (subjectType) {
                case TargetSubject::User: return "user";
                case TargetSubject::Group: return "group";
                default: return "unknown";
            }
        }
    };

    struct OverrideSpec {
        TargetSubject subjectType;
        std::string permName;
        rbac::permission::OverrideOpt effect;
        std::string pattern;
        bool enabled{true};
    };
}
