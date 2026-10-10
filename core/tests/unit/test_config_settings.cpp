// config.yaml / settings JSON for the keys #164 renamed, moved or wired up: sharing.*, vaults.s3.* and the two
// max_connections caps. Upgraded hosts keep their config.yaml (the package never rewrites it), so the old spellings
// must keep their values, the new ones must win when both are present, and the console's JSON must round-trip.

#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "config/config_yaml.hpp"
#include "ops/Config.hpp"
#include "ops/Error.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace vh::test_config_settings {

namespace fs = std::filesystem;
using json = nlohmann::json;

class ConfigSettingsTest : public ::testing::Test {
protected:
    config::Config load(const std::string& yaml, std::vector<std::string>* deprecations = nullptr) {
        path_ = fs::temp_directory_path() / ("vh-config-settings-" + std::to_string(::getpid()) + ".yaml");
        std::ofstream(path_) << yaml;
        return config::loadConfig(path_.string(), deprecations);
    }
    void TearDown() override {
        if (!path_.empty()) fs::remove(path_);
    }
    fs::path path_;
};

bool mentions(const std::vector<std::string>& messages, const std::string& needle) {
    return std::ranges::any_of(messages, [&](const std::string& m) { return m.find(needle) != std::string::npos; });
}

TEST_F(ConfigSettingsTest, MissingSectionsKeepTheDefaults) {
    std::vector<std::string> deprecations;
    const auto cfg = load("dev:\n  enabled: false\n", &deprecations);
    EXPECT_TRUE(cfg.sharing.enabled);
    EXPECT_TRUE(cfg.sharing.enable_internal);
    EXPECT_TRUE(cfg.sharing.enable_anonymous);
    EXPECT_TRUE(cfg.sharing.enable_email_validated);
    EXPECT_EQ(cfg.vaults.s3.default_remote_sync_strategy, "cache");
    EXPECT_EQ(cfg.vaults.s3.default_remote_conflict_policy, "ask");
    EXPECT_EQ(cfg.websocket.max_connections, 1024u);
    EXPECT_EQ(cfg.http_preview.max_connections, 512u);
    EXPECT_TRUE(deprecations.empty());
}

TEST_F(ConfigSettingsTest, NewKeysParse) {
    std::vector<std::string> deprecations;
    const auto cfg = load(R"(
sharing:
  enabled: true
  enable_internal: false
  enable_anonymous: false
  enable_email_validated: true
vaults:
  s3:
    default_remote_sync_strategy: mirror
    default_remote_conflict_policy: keep_newest
websocket_server:
  max_connections: 64
)", &deprecations);
    EXPECT_FALSE(cfg.sharing.enable_internal);
    EXPECT_FALSE(cfg.sharing.enable_anonymous);
    EXPECT_TRUE(cfg.sharing.enable_email_validated);
    EXPECT_EQ(cfg.vaults.s3.default_remote_sync_strategy, "mirror");
    EXPECT_EQ(cfg.vaults.s3.default_remote_conflict_policy, "keep_newest");
    EXPECT_EQ(cfg.websocket.max_connections, 64u);
    EXPECT_TRUE(deprecations.empty());
}

// An upgraded host's config.yaml: the old names still carry the operator's values, and each one is reported.
TEST_F(ConfigSettingsTest, LegacyKeysKeepTheirValuesAndAreReported) {
    std::vector<std::string> deprecations;
    const auto cfg = load(R"(
s3_gateway:
  enabled: false
  default_remote_sync_strategy: sync
  default_remote_conflict_policy: keep_remote
sharing:
  enabled: true
  enable_public_links: false
)", &deprecations);
    EXPECT_FALSE(cfg.sharing.enable_email_validated);
    EXPECT_TRUE(cfg.sharing.enable_anonymous);
    EXPECT_EQ(cfg.vaults.s3.default_remote_sync_strategy, "sync");
    EXPECT_EQ(cfg.vaults.s3.default_remote_conflict_policy, "keep_remote");
    ASSERT_EQ(deprecations.size(), 3u);
    EXPECT_TRUE(mentions(deprecations, "sharing.enable_public_links is deprecated: rename it to sharing.enable_email_validated"));
    EXPECT_TRUE(mentions(deprecations, "s3_gateway.default_remote_sync_strategy is deprecated: move it to vaults.s3"));
    EXPECT_TRUE(mentions(deprecations, "s3_gateway.default_remote_conflict_policy is deprecated: move it to vaults.s3"));
}

TEST_F(ConfigSettingsTest, NewKeysWinOverLegacyOnes) {
    std::vector<std::string> deprecations;
    const auto cfg = load(R"(
s3_gateway:
  default_remote_sync_strategy: sync
  default_remote_conflict_policy: keep_remote
vaults:
  s3:
    default_remote_sync_strategy: cache
    default_remote_conflict_policy: keep_local
sharing:
  enable_public_links: false
  enable_email_validated: true
)", &deprecations);
    EXPECT_TRUE(cfg.sharing.enable_email_validated);
    EXPECT_EQ(cfg.vaults.s3.default_remote_sync_strategy, "cache");
    EXPECT_EQ(cfg.vaults.s3.default_remote_conflict_policy, "keep_local");
    ASSERT_EQ(deprecations.size(), 3u);
    for (const auto& message : deprecations) EXPECT_NE(message.find("ignored"), std::string::npos) << message;
}

// Nothing read the old gateway keys before #164, so they may hold anything; that must not stop the daemon.
TEST_F(ConfigSettingsTest, InvalidLegacyValueIsReportedNotFatal) {
    std::vector<std::string> deprecations;
    config::Config cfg;
    ASSERT_NO_THROW(cfg = load("s3_gateway:\n  default_remote_conflict_policy: keep_both\n", &deprecations));
    EXPECT_EQ(cfg.vaults.s3.default_remote_conflict_policy, "ask");
    ASSERT_EQ(deprecations.size(), 1u);
    EXPECT_TRUE(mentions(deprecations, "'keep_both' is not valid"));
}

TEST_F(ConfigSettingsTest, InvalidNewValueRefusesToLoad) {
    EXPECT_THROW((void)load("vaults:\n  s3:\n    default_remote_sync_strategy: smart\n"), std::invalid_argument);
    EXPECT_THROW((void)load("vaults:\n  s3:\n    default_remote_conflict_policy: overwrite\n"), std::invalid_argument);
}

// What the console edits and Config::save writes back: the new keys only, so a save migrates an old file.
TEST(ConfigSettingsSerialization, YamlRoundTripWritesOnlyTheNewKeys) {
    config::Config cfg;
    cfg.sharing.enable_anonymous = false;
    cfg.sharing.enable_internal = false;
    cfg.vaults.s3.default_remote_sync_strategy = "mirror";
    cfg.vaults.s3.default_remote_conflict_policy = "ask";

    const auto sharing = YAML::convert<config::SharingConfig>::encode(cfg.sharing);
    const auto vaults = YAML::convert<config::VaultsConfig>::encode(cfg.vaults);
    const auto gateway = YAML::convert<config::S3GatewayConfig>::encode(cfg.s3_gateway);
    EXPECT_FALSE(sharing["enable_public_links"]);
    EXPECT_FALSE(gateway["default_remote_sync_strategy"]);
    EXPECT_FALSE(gateway["default_remote_conflict_policy"]);

    config::SharingConfig sharingBack;
    config::VaultsConfig vaultsBack;
    ASSERT_TRUE(YAML::convert<config::SharingConfig>::decode(YAML::Load(YAML::Dump(sharing)), sharingBack));
    ASSERT_TRUE(YAML::convert<config::VaultsConfig>::decode(YAML::Load(YAML::Dump(vaults)), vaultsBack));
    EXPECT_EQ(json(sharingBack), json(cfg.sharing));
    EXPECT_EQ(json(vaultsBack), json(cfg.vaults));
}

TEST(ConfigSettingsSerialization, JsonRoundTripAndLegacyAliases) {
    config::Config cfg;
    cfg.sharing.enable_email_validated = false;
    cfg.vaults.s3.default_remote_conflict_policy = "keep_newest";
    cfg.websocket.max_connections = 77;

    const json doc = cfg;
    EXPECT_TRUE(doc.at("vaults").at("s3").contains("default_remote_sync_strategy"));
    EXPECT_FALSE(doc.at("s3_gateway").contains("default_remote_sync_strategy"));
    EXPECT_FALSE(doc.at("sharing").contains("enable_public_links"));
    const config::Config back(doc);
    EXPECT_EQ(json(back), doc);

    // A pre-#164 API client: old spellings apply when the new ones are absent...
    json legacy = doc;
    legacy.erase("vaults");
    legacy["s3_gateway"]["default_remote_sync_strategy"] = "sync";
    legacy["sharing"].erase("enable_email_validated");
    legacy["sharing"]["enable_public_links"] = false;
    const config::Config fromLegacy(legacy);
    EXPECT_EQ(fromLegacy.vaults.s3.default_remote_sync_strategy, "sync");
    EXPECT_FALSE(fromLegacy.sharing.enable_email_validated);

    // ...and lose to them when both are sent.
    json both = doc;
    both["s3_gateway"]["default_remote_sync_strategy"] = "sync";
    both["sharing"]["enable_public_links"] = true;
    const config::Config fromBoth(both);
    EXPECT_EQ(fromBoth.vaults.s3.default_remote_sync_strategy, "cache");
    EXPECT_FALSE(fromBoth.sharing.enable_email_validated);
}

// settings.update and the CLI share ops::config's validation.
TEST(ConfigSettingsValidation, RefusesUnknownVaultDefaultsAndZeroCaps) {
    const json current = config::Registry::get();

    auto doc = current;
    doc["vaults"]["s3"]["default_remote_conflict_policy"] = "keep_both";
    EXPECT_THROW(ops::config::validateSettings(doc), ops::Invalid);

    doc = current;
    doc["vaults"]["s3"]["default_remote_sync_strategy"] = "smart";
    EXPECT_THROW(ops::config::validateSettings(doc), ops::Invalid);

    doc = current;
    doc["websocket_server"]["max_connections"] = 0;
    EXPECT_THROW(ops::config::validateSettings(doc), ops::Invalid);

    doc = current;
    doc["http_preview_server"]["max_connections"] = 0;
    EXPECT_THROW(ops::config::validateSettings(doc), ops::Invalid);

    doc = current;
    doc["vaults"]["s3"]["default_remote_conflict_policy"] = "ask";
    doc["sharing"]["enable_anonymous"] = false;
    EXPECT_NO_THROW(ops::config::validateSettings(doc));
}

// The shipped default config uses the new keys only (an install never starts with a deprecation warning).
TEST(ShippedConfig, UsesTheNewKeysWithTheirDefaults) {
    // This tree's deploy/config/config.yaml (VH_PATH_TO_CONFIG may point at another checkout's).
    const auto path = fs::path(VH_TEST_ASSETS_DIR).parent_path().parent_path() / "deploy" / "config" / "config.yaml";
    ASSERT_TRUE(fs::exists(path)) << path;
    std::vector<std::string> deprecations;
    const auto cfg = config::loadConfig(path.string(), &deprecations);
    EXPECT_TRUE(deprecations.empty()) << deprecations.front();
    EXPECT_TRUE(cfg.sharing.enabled);
    EXPECT_TRUE(cfg.sharing.enable_internal);
    EXPECT_TRUE(cfg.sharing.enable_anonymous);
    EXPECT_TRUE(cfg.sharing.enable_email_validated);
    EXPECT_EQ(cfg.vaults.s3.default_remote_sync_strategy, "cache");
    EXPECT_EQ(cfg.vaults.s3.default_remote_conflict_policy, "ask");
    EXPECT_EQ(cfg.websocket.max_connections, 1024u);
    EXPECT_EQ(cfg.http_preview.max_connections, 512u);
}

}
