#include "vault/model/Vault.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using vh::vault::model::Vault;

TEST(VaultNamingTest, SlugifiesDisplayNameForDefaultExternalName) {
    EXPECT_EQ(vh::vault::model::slugifyName("My Photos"), "my-photos");
    EXPECT_EQ(vh::vault::model::slugifyName("  My---Photos!! "), "my-photos");
    EXPECT_EQ(vh::vault::model::slugifyName("A"), "a00");
    EXPECT_EQ(vh::vault::model::slugifyName(""), "vault");
}

TEST(VaultNamingTest, ValidatesS3SafeSlugAndBucketNames) {
    EXPECT_TRUE(vh::vault::model::isValidVaultSlug("my-photos"));
    EXPECT_TRUE(vh::vault::model::isValidS3Name("archive-2026"));

    EXPECT_FALSE(vh::vault::model::isValidVaultSlug("My-Photos"));
    EXPECT_FALSE(vh::vault::model::isValidVaultSlug("-photos"));
    EXPECT_FALSE(vh::vault::model::isValidVaultSlug("photos-"));
    EXPECT_FALSE(vh::vault::model::isValidVaultSlug("ab"));
    EXPECT_FALSE(vh::vault::model::isValidVaultSlug("my_photos"));
}

TEST(VaultNamingTest, ValidatesFuseNameAsSinglePathComponent) {
    EXPECT_TRUE(vh::vault::model::isValidFuseName("My Photos"));
    EXPECT_TRUE(vh::vault::model::isValidFuseName("my_photos"));

    EXPECT_FALSE(vh::vault::model::isValidFuseName(""));
    EXPECT_FALSE(vh::vault::model::isValidFuseName("."));
    EXPECT_FALSE(vh::vault::model::isValidFuseName(".."));
    EXPECT_FALSE(vh::vault::model::isValidFuseName("my/photos"));
    EXPECT_FALSE(vh::vault::model::isValidFuseName("my\\photos"));
}

TEST(VaultNamingTest, EffectiveFuseNameUsesOverrideThenSlug) {
    Vault vault;
    vault.name = "My Photos";
    vault.slug = "my-photos";

    EXPECT_EQ(vault.effectiveFuseName(), "my-photos");

    vault.fuse_name = "My Photos";
    EXPECT_EQ(vault.effectiveFuseName(), "My Photos");
}

TEST(VaultNamingTest, JsonIncludesSlugFuseOverrideAndEffectiveName) {
    Vault vault;
    vault.id = 7;
    vault.name = "My Photos";
    vault.slug = "my-photos";
    vault.fuse_name = "Photos";
    vault.owner_id = 1;
    vault.mount_point = "QQQAF9_HWXSAFJXY6NH6EESSHVFN05RPC";
    vault.created_at = 0;

    nlohmann::json out = vault;

    EXPECT_EQ(out.at("name").get<std::string>(), "My Photos");
    EXPECT_EQ(out.at("slug").get<std::string>(), "my-photos");
    EXPECT_EQ(out.at("fuse_name").get<std::string>(), "Photos");
    EXPECT_EQ(out.at("effective_fuse_name").get<std::string>(), "Photos");
}
