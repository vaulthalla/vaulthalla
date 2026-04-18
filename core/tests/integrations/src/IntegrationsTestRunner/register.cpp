#include "IntegrationsTestRunner.hpp"
#include "cli/Context.hpp"

namespace vh::test::integration {
    void IntegrationsTestRunner::registerStdoutContains(const std::string& path, std::string needle) {
        expectations_by_path_[path].must_have.push_back(std::move(needle));
    }
    void IntegrationsTestRunner::registerStdoutNotContains(const std::string& path, std::string needle) {
        expectations_by_path_[path].must_not_have.push_back(std::move(needle));
    }
    void IntegrationsTestRunner::registerStdoutContains(const std::string& path, std::vector<std::string> needles) {
        auto& e = expectations_by_path_[path].must_have;
        e.insert(e.end(),
                 std::make_move_iterator(needles.begin()),
                 std::make_move_iterator(needles.end()));
    }
    void IntegrationsTestRunner::registerStdoutNotContains(const std::string& path, std::vector<std::string> needles) {
        auto& e = expectations_by_path_[path].must_not_have;
        e.insert(e.end(),
                 std::make_move_iterator(needles.begin()),
                 std::make_move_iterator(needles.end()));
    }

    void IntegrationsTestRunner::registerAllContainsAssertions() {
        const std::vector<std::string> user_info_fields = {"User ID", "User", "Email", "Role"};
        const std::vector<std::string> vault_info_fields = {"ID", "Name", "Owner ID", "Quota"};
        const std::vector<std::string> vault_list_fields = {"ID", "NAME", "OWNER", "QUOTA", "DESCRIPTION"};
        const std::vector<std::string> group_info_fields = {"Group ID", "Name"};
        const std::vector<std::string> role_info_fields = {"ID", "Name", "Type", "Permissions", "Created At", "Updated At"};

        for (const auto& entity : cli::Context::ENTITIES) {
            for (const auto& action : cli::Context::ACTIONS) {
                const auto path = std::string(entity) + "/" + std::string(action);
                registerStdoutNotContains(path, {"Traceback", "Exception", "Error", "invalid", "not found", "failed", "unrecognized"});
                if (action == "info") {
                    if (entity == "user") registerStdoutContains(path, user_info_fields);
                    else if (entity == "vault") registerStdoutContains(path, vault_info_fields);
                    else if (entity == "group") registerStdoutContains(path, group_info_fields);
                    else if (entity == "role") registerStdoutContains(path, role_info_fields);
                } else if (action == "list") {
                    if (entity == "user") registerStdoutContains(path, {"ID", "Name", "Email", "Role"});
                    else if (entity == "vault") registerStdoutContains(path, vault_list_fields);
                    else if (entity == "group") registerStdoutContains(path, {std::string("ID"), "Name"});
                    else if (entity == "role") registerStdoutContains(path, {"ID", "Name", "Type", "Permissions"});
                }
            }
        }
    }
}
