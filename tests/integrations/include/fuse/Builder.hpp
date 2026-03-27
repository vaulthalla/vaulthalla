#pragma once

#include "Context.hpp"
#include "TestStage.hpp"

#include <memory>
#include <filesystem>

namespace vh::test::integration::fuse {
    struct Builder {
        explicit Builder(FuseScenarioContext scenario)
            : ctx_(std::move(scenario)) {}

        static Builder make(BuilderSpec&& spec);

        void buildAssignVRole(VaultRoleSpec&& spec);

        void makeUser(UserSpec&& userSpec = {});

        void makeGroup(std::string&& nameSeed = "group/create");

        void makeOverride(OverrideSpec&& spec) const;

        void makeTestCase(FuseTestCaseSpec&& spec);

        [[nodiscard]] TestStage exec() const;

        [[nodiscard]] std::pair<FuseScenarioContext, FuseSubject> scenario() const { return {ctx_, subject_}; }

        void addUserToGroup();

    private:
        FuseScenarioContext ctx_{};
        FuseSubject subject_{};
        std::vector<uint32_t> uids_, gids_;

        [[nodiscard]] std::vector<std::shared_ptr<TestCase>> runSteps() const;

        static std::shared_ptr<storage::Engine> makeVault();

        static void seed_vault_tree(uid_t admin_uid, const std::filesystem::path& root, const std::string& base = "perm_seed");
    };
}
