#include "fs/model/Directory.hpp"
#include "fs/model/File.hpp"
#include "share/TargetResolver.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>

using namespace vh::share;

namespace {

std::shared_ptr<vh::fs::model::Directory> dir(
    const uint32_t id,
    const uint32_t vaultId,
    const std::string& path,
    const std::optional<int32_t> parentId = std::nullopt
) {
    auto entry = std::make_shared<vh::fs::model::Directory>();
    entry->id = id;
    entry->vault_id = vaultId;
    entry->parent_id = parentId;
    entry->path = path;
    entry->name = path == "/" ? "" : std::filesystem::path(path).filename().string();
    entry->created_at = entry->updated_at = 1'800'000'000;
    return entry;
}

std::shared_ptr<vh::fs::model::File> makeFile(
    const uint32_t id,
    const uint32_t vaultId,
    const std::string& path,
    const std::optional<int32_t> parentId = std::nullopt
) {
    auto entry = std::make_shared<vh::fs::model::File>();
    entry->id = id;
    entry->vault_id = vaultId;
    entry->parent_id = parentId;
    entry->path = path;
    entry->name = std::filesystem::path(path).filename().string();
    entry->mime_type = "application/pdf";
    entry->created_at = entry->updated_at = 1'800'000'000;
    return entry;
}

class FakeEntryProvider final : public TargetEntryProvider {
public:
    std::unordered_map<uint32_t, std::shared_ptr<vh::fs::model::Entry>> by_id;
    std::unordered_map<std::string, std::shared_ptr<vh::fs::model::Entry>> by_path;
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<vh::fs::model::Entry>>> children;

    void add(const std::shared_ptr<vh::fs::model::Entry>& entry) {
        by_id[entry->id] = entry;
        by_path[key(*entry->vault_id, entry->path.string())] = entry;
        if (entry->parent_id) children[*entry->parent_id].push_back(entry);
    }

    std::shared_ptr<vh::fs::model::Entry> getEntryById(const uint32_t entryId) override {
        if (!by_id.contains(entryId)) return nullptr;
        return by_id.at(entryId);
    }

    std::shared_ptr<vh::fs::model::Entry> getEntryByVaultPath(
        const uint32_t vaultId,
        const std::string_view vaultPath
    ) override {
        const auto k = key(vaultId, std::string(vaultPath));
        if (!by_path.contains(k)) return nullptr;
        return by_path.at(k);
    }

    std::vector<std::shared_ptr<vh::fs::model::Entry>> listChildren(const uint32_t parentEntryId) override {
        if (!children.contains(parentEntryId)) return {};
        return children.at(parentEntryId);
    }

private:
    static std::string key(const uint32_t vaultId, const std::string& path) {
        return std::to_string(vaultId) + ":" + path;
    }
};

Principal makeFsPrincipal() {
    Principal principal;
    principal.share_id = "00000000-0000-4000-8000-000000000101";
    principal.share_session_id = "00000000-0000-4000-8000-000000000201";
    principal.vault_id = 42;
    principal.root_entry_id = 10;
    principal.root_path = "/shared";
    principal.grant.vault_id = 42;
    principal.grant.root_entry_id = 10;
    principal.grant.root_path = "/shared";
    principal.grant.target_type = TargetType::Directory;
    principal.grant.allowed_ops = bit(Operation::Metadata) | bit(Operation::List);
    principal.expires_at = 1'800'003'600;
    return principal;
}

std::shared_ptr<FakeEntryProvider> providerWithSharedTree() {
    auto provider = std::make_shared<FakeEntryProvider>();
    provider->add(dir(10, 42, "/shared"));
    provider->add(dir(11, 42, "/shared/reports", 10));
    provider->add(makeFile(12, 42, "/shared/reports/q1.pdf", 11));
    provider->add(makeFile(13, 42, "/shared/readme.txt", 10));
    provider->add(dir(20, 42, "/shared_evil"));
    return provider;
}

}

TEST(ShareFsResolutionTest, ResolvesRootPathAndMetadataGrant) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    const auto principal = makeFsPrincipal();

    const auto target = resolver.resolve(principal, {
        .path = "/shared",
        .operation = Operation::Metadata
    });

    EXPECT_EQ(target.vault_id, 42u);
    EXPECT_EQ(target.root_entry_id, 10u);
    EXPECT_EQ(target.vault_path, "/shared");
    EXPECT_EQ(target.share_path, "/");
    ASSERT_NE(target.entry, nullptr);
    EXPECT_EQ(target.entry->id, 10u);
}

TEST(ShareFsResolutionTest, ResolvesNormalizedDescendantAndDuplicateSlashes) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    const auto principal = makeFsPrincipal();

    const auto target = resolver.resolve(principal, {
        .path = "//shared//reports///q1.pdf",
        .operation = Operation::Metadata
    });

    EXPECT_EQ(target.vault_path, "/shared/reports/q1.pdf");
    EXPECT_EQ(target.share_path, "/reports/q1.pdf");
    EXPECT_EQ(target.target_type, TargetType::File);
}

TEST(ShareFsResolutionTest, DeniesPathEscapeAndPrefixTrick) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    const auto principal = makeFsPrincipal();

    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "/shared/../secret.txt",
        .operation = Operation::Metadata
    }); }, std::runtime_error);

    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "../shared/reports/q1.pdf",
        .operation = Operation::Metadata
    }); }, std::runtime_error);

    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "/shared_evil",
        .operation = Operation::Metadata
    }); }, std::runtime_error);
}

TEST(ShareFsResolutionTest, DeniesCrossVaultAndMissingOperation) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    auto principal = makeFsPrincipal();

    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "/shared/reports",
        .operation = Operation::Metadata,
        .vault_id = 43
    }); }, std::runtime_error);

    principal.grant.allowed_ops &= ~bit(Operation::List);
    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "/shared/reports",
        .operation = Operation::List,
        .expected_target_type = TargetType::Directory
    }); }, std::runtime_error);
}

TEST(ShareFsResolutionTest, EnforcesListDirectoryTargetAndRejectsFileTargetList) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    auto principal = makeFsPrincipal();

    const auto target = resolver.resolve(principal, {
        .path = "/shared/reports",
        .operation = Operation::List,
        .expected_target_type = TargetType::Directory
    });
    EXPECT_EQ(target.target_type, TargetType::Directory);

    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "/shared/reports/q1.pdf",
        .operation = Operation::List,
        .expected_target_type = TargetType::Directory
    }); }, std::runtime_error);

    auto filePrincipal = principal;
    filePrincipal.root_entry_id = 12;
    filePrincipal.root_path = "/shared/reports/q1.pdf";
    filePrincipal.grant.root_entry_id = 12;
    filePrincipal.grant.root_path = "/shared/reports/q1.pdf";
    filePrincipal.grant.target_type = TargetType::File;
    EXPECT_THROW({ (void)resolver.resolve(filePrincipal, {
        .path = "/shared/reports/q1.pdf",
        .operation = Operation::List,
        .expected_target_type = TargetType::Directory
    }); }, std::runtime_error);
}

TEST(ShareFsResolutionTest, FailsClosedWhenRootEntryMovedOrTargetMissing) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    auto principal = makeFsPrincipal();

    provider->by_id.at(10)->path = "/renamed";
    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "/shared",
        .operation = Operation::Metadata
    }); }, std::runtime_error);

    provider->by_id.at(10)->path = "/shared";
    EXPECT_THROW({ (void)resolver.resolve(principal, {
        .path = "/shared/missing.txt",
        .operation = Operation::Metadata
    }); }, std::runtime_error);
}

TEST(ShareFsResolutionTest, ListsChildrenOnlyWhenTheyRemainInsideScope) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    const auto principal = makeFsPrincipal();

    const auto target = resolver.resolve(principal, {
        .path = "/shared",
        .operation = Operation::List,
        .expected_target_type = TargetType::Directory
    });
    auto children = resolver.listChildren(principal, target);
    ASSERT_EQ(children.size(), 2u);

    provider->children[10].push_back(makeFile(99, 42, "/outside.txt", 10));
    EXPECT_THROW({ (void)resolver.listChildren(principal, target); }, std::runtime_error);
}

TEST(ShareFsResolutionTest, CanConvertShareRelativePathForInternalCallers) {
    const auto provider = providerWithSharedTree();
    const TargetResolver resolver(provider);
    const auto principal = makeFsPrincipal();

    const auto target = resolver.resolve(principal, {
        .path = "/reports/q1.pdf",
        .operation = Operation::Metadata,
        .path_mode = TargetPathMode::ShareRelative
    });

    EXPECT_EQ(target.vault_path, "/shared/reports/q1.pdf");
    EXPECT_EQ(target.share_path, "/reports/q1.pdf");
}
