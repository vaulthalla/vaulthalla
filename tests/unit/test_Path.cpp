#include <gtest/gtest.h>
#include "fs/model/Path.hpp"

#include <filesystem>

using namespace vh::fs::model;

class PathTest : public ::testing::Test {
protected:
    const std::filesystem::path vaultFuseMount = "/admin", vaultBackingMount = "/QQQAF9_HWXSAFJXY6NH6EESSHVFN05RPC";
    void SetUp() override { /* no-op for now */ }
};

TEST_F(PathTest, AbsPath_VaultRoot) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path abs = p.absPath("Invoice.pdf", PathType::VAULT_ROOT);
    EXPECT_EQ(abs, "/mnt/vaulthalla" + vaultFuseMount.string() + "/Invoice.pdf");
}

TEST_F(PathTest, RelPath_VaultRoot) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path rel = p.relPath("/mnt/vaulthalla" + vaultFuseMount.string() + "/Invoice.pdf", PathType::VAULT_ROOT);
    EXPECT_EQ(rel, "Invoice.pdf");
}

TEST_F(PathTest, AbsRelToRoot_StripsMountPrefix) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToRoot("/mnt/vaulthalla" + vaultFuseMount.string() + "/Invoice.pdf", PathType::VAULT_ROOT);
    EXPECT_EQ(out, "/Invoice.pdf");  // normalized under vault root
}

TEST_F(PathTest, AbsRelToRoot_NestedPath) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToRoot("/mnt/vaulthalla" + vaultFuseMount.string() + "/projects/test.txt", PathType::VAULT_ROOT);
    EXPECT_EQ(out, "/projects/test.txt");
}

TEST_F(PathTest, AbsRelToRoot_NotUnderRoot) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToRoot("/etc/passwd", PathType::VAULT_ROOT);
    // Should just fall back to filename
    EXPECT_EQ(out.filename(), "passwd");
}

TEST_F(PathTest, CacheRootPath) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path abs = p.absPath("file.tmp", PathType::CACHE_ROOT);
    EXPECT_EQ(abs.string(), "/var/lib/vaulthalla/.cache" + vaultBackingMount.string() + "/file.tmp");
}

TEST_F(PathTest, RelPath_VaultRoot_Subdir) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path rel = p.relPath("/mnt/vaulthalla" + vaultFuseMount.string() + "/docs/report.txt", PathType::VAULT_ROOT);
    EXPECT_EQ(rel, "docs/report.txt");
}

TEST_F(PathTest, RelPath_FuseRoot) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path rel = p.relPath("/mnt/vaulthalla" + vaultFuseMount.string() + "/docs/report.txt", PathType::FUSE_ROOT);
    EXPECT_EQ(rel, stripLeadingSlash(vaultFuseMount).string() + "/docs/report.txt");
}

TEST_F(PathTest, AbsRelToRoot_SimpleFile) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToRoot("/mnt/vaulthalla" + vaultFuseMount.string() + "/note.txt", PathType::VAULT_ROOT);
    EXPECT_EQ(out, "/note.txt");
}

TEST_F(PathTest, AbsRelToRoot_NormalizesDotDot) {
    Path p(vaultFuseMount, vaultBackingMount);
    // Simulate a user path with extra traversal
    const std::filesystem::path out = p.absRelToRoot("/mnt/vaulthalla" + vaultFuseMount.string() + "/../admin/Invoice.pdf", PathType::VAULT_ROOT);
    EXPECT_EQ(out.string(), "/Invoice.pdf");  // should normalize away the ".."
}

TEST_F(PathTest, AbsRelToRoot_DeepSubdir) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToRoot("/mnt/vaulthalla" + vaultFuseMount.string() + "/projects/2025/report/final.docx", PathType::VAULT_ROOT);
    EXPECT_EQ(out, "/projects/2025/report/final.docx");
}

TEST_F(PathTest, AbsPath_BackupRoot) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path abs = p.absPath("shadow.db", PathType::BACKING_VAULT_ROOT);
    EXPECT_EQ(abs, "/var/lib/vaulthalla" + vaultBackingMount.string() + "/shadow.db");
}

TEST_F(PathTest, AbsRelToRoot_ResolvePath) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path abs = p.absRelToRoot("/mnt/vaulthalla" + vaultFuseMount.string() + "/test-assets/sample.jpg", PathType::VAULT_ROOT);
    EXPECT_EQ(abs, "/test-assets/sample.jpg");
}

TEST_F(PathTest, AbsRelToAbsOther_ConvertPaths) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path abs = p.absRelToAbsRel("" + vaultFuseMount.string() + "/test.txt", PathType::FUSE_ROOT, PathType::VAULT_ROOT);
    EXPECT_EQ(abs, "/test.txt");
}

TEST_F(PathTest, AbsRelToAbsOther_OutsideRootFallsBack) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path abs = p.absRelToAbsRel("/etc/passwd", PathType::VAULT_ROOT, PathType::CACHE_ROOT);
    EXPECT_EQ(abs.filename(), "passwd"); // Should collapse to filename
}

TEST_F(PathTest, AbsRelToAbsOther_BackupRootToVault) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path abs = p.absRelToAbsRel("/var/lib/vaulthalla" + vaultBackingMount.string() + "/shadow.db",
        PathType::BACKING_VAULT_ROOT, PathType::VAULT_ROOT);
    EXPECT_EQ(abs, "/shadow.db");
}

TEST_F(PathTest, AbsRelToRoot_EmptyString) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToRoot("", PathType::VAULT_ROOT);
    EXPECT_TRUE(out == std::filesystem::path("/"));  // should return root if empty
}

TEST_F(PathTest, AbsRelToAbsOther_ReduceFuseToVault) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToAbsRel("" + vaultFuseMount.string() + "/sample_data/Invoice-102-Cooper-Larson.pdf", PathType::FUSE_ROOT, PathType::VAULT_ROOT);
    EXPECT_EQ(out, "/sample_data/Invoice-102-Cooper-Larson.pdf");  // should treat as absolute under vault root
}

TEST_F(PathTest, AbsRelToRoot_VaultToFuse) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToRoot(p.vaultRoot, PathType::FUSE_ROOT);
    EXPECT_EQ(out, vaultFuseMount.string());
}

TEST_F(PathTest, AbsRelToAbsRel_VaultToFuse) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToAbsRel("/docs/report.txt", PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    EXPECT_EQ(out, vaultFuseMount.string() + "/docs/report.txt");
}

TEST_F(PathTest, AbsRelToAbsRel_VaultBaseToFuse) {
    Path p(vaultFuseMount, vaultBackingMount);
    const std::filesystem::path out = p.absRelToAbsRel("/", PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    EXPECT_EQ(out, vaultFuseMount.string());
}
