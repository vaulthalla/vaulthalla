#include "db/Transactions.hpp"
#include "db/query/fs/Directory.hpp"
#include "db/query/fs/File.hpp"
#include "fs/cache/Registry.hpp"
#include "fs/model/Directory.hpp"
#include "fs/model/File.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"

#include <gtest/gtest.h>
#include <paths.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

struct SeedIds {
    uint32_t userId{};
    uint32_t rootId{};
    uint32_t vaultId{};
    uint32_t vaultRootId{};
};

std::string aliasFor(const char c) {
    return std::string(33, c);
}

class FsCacheDeleteTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;
    inline static SeedIds ids{};

    std::shared_ptr<vh::fs::cache::Registry> previousCache;
    std::filesystem::path oldBackingPath;
    std::filesystem::path oldMountPath;
    std::filesystem::path testRoot;
    bool installedCache{false};

    static bool hasDbEnv() {
        return std::getenv("VH_TEST_DB_USER") &&
               std::getenv("VH_TEST_DB_PASS") &&
               std::getenv("VH_TEST_DB_HOST") &&
               std::getenv("VH_TEST_DB_PORT") &&
               std::getenv("VH_TEST_DB_NAME");
    }

    static void SetUpTestSuite() {
        if (!hasDbEnv()) {
            skipTests = true;
            std::cout << "[test_fs_cache_delete] Skipping db tests due to missing environment variables." << std::endl;
            return;
        }

        vh::paths::enableTestMode();
        vh::db::Transactions::init();
        vh::db::seed::nuke_and_recreate_schema_public();
        vh::db::Transactions::dbPool_->initPreparedStatements();

        ids = vh::db::Transactions::exec("FsCacheDeleteTest::seed", [](pqxx::work& txn) {
            SeedIds seeded;
            seeded.userId = txn.exec(
                "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
                pqxx::params{"fs_cache_delete_user", "fs-cache-delete@vaulthalla.test", "hash"}
            ).one_field().as<uint32_t>();

            seeded.vaultId = txn.exec(
                "INSERT INTO vault (type, name, owner_id, mount_point) VALUES ($1, $2, $3, $4) RETURNING id",
                pqxx::params{"local", "Cache Delete Vault", seeded.userId, "cache_delete_vault"}
            ).one_field().as<uint32_t>();

            seeded.rootId = txn.exec(
                "INSERT INTO fs_entry (vault_id, parent_id, name, base32_alias, created_by, last_modified_by, path, inode, is_system) "
                "VALUES (NULL, NULL, $1, $2, $3, $3, $4, $5, TRUE) RETURNING id",
                pqxx::params{"/", aliasFor('R'), seeded.userId, "/", 1}
            ).one_field().as<uint32_t>();
            txn.exec(
                "INSERT INTO directories (fs_entry_id, subdirectory_count) VALUES ($1, $2)",
                pqxx::params{seeded.rootId, 1}
            );

            seeded.vaultRootId = txn.exec(
                "INSERT INTO fs_entry (vault_id, parent_id, name, base32_alias, created_by, last_modified_by, path, inode) "
                "VALUES ($1, $2, $3, $4, $5, $5, $6, $7) RETURNING id",
                pqxx::params{seeded.vaultId, seeded.rootId, "cache_vault", aliasFor('V'), seeded.userId, "/", 2}
            ).one_field().as<uint32_t>();
            txn.exec("INSERT INTO directories (fs_entry_id) VALUES ($1)", pqxx::params{seeded.vaultRootId});

            return seeded;
        });
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";

        oldBackingPath = vh::paths::backingPath;
        oldMountPath = vh::paths::mountPath;
        testRoot = std::filesystem::temp_directory_path() / "vh_fs_cache_delete_unit";
        std::filesystem::remove_all(testRoot);
        vh::paths::backingPath = testRoot / "backing";
        vh::paths::mountPath = testRoot / "mount";
        std::filesystem::create_directories(vh::paths::backingPath);
        std::filesystem::create_directories(vh::paths::mountPath);

        previousCache = vh::runtime::Deps::get().fsCache;
        vh::runtime::Deps::get().fsCache = std::make_shared<vh::fs::cache::Registry>();
        installedCache = true;
    }

    void TearDown() override {
        if (installedCache) {
            vh::runtime::Deps::get().fsCache = std::move(previousCache);
            std::filesystem::remove_all(testRoot);
            vh::paths::backingPath = oldBackingPath;
            vh::paths::mountPath = oldMountPath;
        }
    }

    std::shared_ptr<vh::fs::model::Directory> vaultRoot() const {
        auto entry = vh::runtime::Deps::get().fsCache->getEntry("/cache_vault");
        if (!entry || !entry->isDirectory()) throw std::runtime_error("Seeded vault root missing from fs cache");
        return std::static_pointer_cast<vh::fs::model::Directory>(entry);
    }

    std::shared_ptr<vh::fs::model::Directory> makeDirectory(
        const std::filesystem::path& fusePath,
        const std::string& base32Alias
    ) const {
        auto parent = vaultRoot();
        auto dir = std::make_shared<vh::fs::model::Directory>();
        dir->vault_id = ids.vaultId;
        dir->parent_id = ids.vaultRootId;
        dir->name = fusePath.filename().string();
        dir->base32_alias = base32Alias;
        dir->created_by = ids.userId;
        dir->last_modified_by = ids.userId;
        dir->path = "/" / fusePath.filename();
        dir->fuse_path = fusePath;
        dir->backing_path = parent->backing_path / base32Alias;
        dir->inode = vh::runtime::Deps::get().fsCache->assignInode(fusePath);
        dir->mode = 0755;
        return dir;
    }

    std::shared_ptr<vh::fs::model::File> makeFile(
        const std::filesystem::path& fusePath,
        const std::string& base32Alias
    ) const {
        auto parent = vaultRoot();
        auto file = std::make_shared<vh::fs::model::File>();
        file->vault_id = ids.vaultId;
        file->parent_id = ids.vaultRootId;
        file->name = fusePath.filename().string();
        file->base32_alias = base32Alias;
        file->created_by = ids.userId;
        file->last_modified_by = ids.userId;
        file->path = "/" / fusePath.filename();
        file->fuse_path = fusePath;
        file->backing_path = parent->backing_path / base32Alias;
        file->inode = vh::runtime::Deps::get().fsCache->assignInode(fusePath);
        file->mode = 0644;
        file->size_bytes = 4;
        file->mime_type = "text/plain";
        file->content_hash = "hash";
        return file;
    }
};

TEST_F(FsCacheDeleteTest, EvictedDirectoryPathCanBeRecreatedWithFreshMetadata) {
    const std::filesystem::path path = "/cache_vault/reupload";

    auto first = makeDirectory(path, aliasFor('A'));
    first->id = vh::db::query::fs::Directory::upsertDirectory(first);
    vh::runtime::Deps::get().fsCache->cacheEntry(first);

    ASSERT_TRUE(vh::runtime::Deps::get().fsCache->entryExists(path));
    const auto firstId = first->id;

    vh::db::query::fs::Directory::deleteEmptyDirectory(first->id);
    ASSERT_TRUE(vh::runtime::Deps::get().fsCache->entryExists(path));

    vh::runtime::Deps::get().fsCache->evictPath(path);
    EXPECT_FALSE(vh::runtime::Deps::get().fsCache->entryExists(path));

    auto second = makeDirectory(path, aliasFor('B'));
    second->id = vh::db::query::fs::Directory::upsertDirectory(second);
    vh::runtime::Deps::get().fsCache->cacheEntry(second);

    const auto recreated = vh::runtime::Deps::get().fsCache->getEntry(path);
    ASSERT_NE(recreated, nullptr);
    EXPECT_TRUE(recreated->isDirectory());
    EXPECT_EQ(recreated->id, second->id);
    EXPECT_NE(recreated->id, firstId);
    EXPECT_EQ(recreated->fuse_path, path);
    EXPECT_EQ(recreated->base32_alias, aliasFor('B'));
}

TEST_F(FsCacheDeleteTest, DeleteFileTrashesAndMarksDeletedWithRegisteredStatements) {
    const std::filesystem::path path = "/cache_vault/delete-me.txt";
    auto file = makeFile(path, aliasFor('F'));
    file->id = vh::db::query::fs::File::upsertFile(file);
    vh::runtime::Deps::get().fsCache->cacheEntry(file);

    ASSERT_NO_THROW(vh::db::query::fs::File::deleteFile(ids.userId, file));

    struct DeletedRow {
        uint32_t liveCount{};
        uint32_t trashedCount{};
        std::string backingPath;
        bool markedDeleted{};
    };

    const auto row = vh::db::Transactions::exec("FsCacheDeleteTest::assertDeletedFile", [&](pqxx::work& txn) {
        DeletedRow out;
        out.liveCount = txn.exec(
            "SELECT COUNT(*) FROM fs_entry WHERE id = $1",
            pqxx::params{file->id}
        ).one_field().as<uint32_t>();
        const auto trashed = txn.exec(
            "SELECT backing_path, deleted_at IS NOT NULL AS marked_deleted "
            "FROM files_trashed WHERE base32_alias = $1",
            pqxx::params{file->base32_alias}
        );
        out.trashedCount = static_cast<uint32_t>(trashed.size());
        if (!trashed.empty()) {
            out.backingPath = trashed.one_row()["backing_path"].as<std::string>();
            out.markedDeleted = trashed.one_row()["marked_deleted"].as<bool>();
        }
        return out;
    });

    EXPECT_EQ(row.liveCount, 0u);
    EXPECT_EQ(row.trashedCount, 1u);
    EXPECT_EQ(row.backingPath, file->backing_path.string());
    EXPECT_TRUE(row.markedDeleted);
}

} // namespace
