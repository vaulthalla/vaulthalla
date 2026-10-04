#pragma once

// Shared helpers and the S3GatewayDbTest fixture for the S3 gateway suites (split across
// test_s3_gateway_*.cpp so no single translation unit dominates the per-file build). Free helpers are
// `inline` in a named namespace: one definition program-wide, no unused-function warnings in files that
// use only some of them, one fixture type for gtest across translation units, and no collisions with other
// test files' anonymous-namespace helpers when unity builds put them in the same chunk.

#include "db/Transactions.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/s3/Gateway.hpp"
#include "db/query/vault/Vault.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "config/config_yaml.hpp"
#include "fs/Filesystem.hpp"
#include "fs/model/Path.hpp"
#include "identities/User.hpp"
#include "protocols/s3/CredentialManager.hpp"
#include "ops/Error.hpp"
#include "ops/S3Gateway.hpp"
#include "protocols/s3/MultipartStore.hpp"
#include "protocols/s3/ObjectStore.hpp"
#include "protocols/s3/GatewayService.hpp"
#include "protocols/s3/Router.hpp"
#include "protocols/s3/Session.hpp"
#include "protocols/s3/SigV4.hpp"
#include "protocols/s3/Xml.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/commands/all.hpp"
#include "protocols/ws/handler/S3Gateway.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "rbac/fs/glob/model/Pattern.hpp"
#include "rbac/permission/admin/S3Gateway.hpp"
#include "rbac/s3/policy/Evaluator.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"
#include "UsageManager.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/pricing/GatewayPriceEstimate.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/Vault.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <paths.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace vh::test::s3_gateway {

namespace beast = boost::beast;

inline std::string s3GatewayUniqueSuffix(const std::string& label) {
    return label + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

struct ConfigRestore {
    vh::config::Config previous;

    explicit ConfigRestore(vh::config::Config cfg)
        : previous(std::move(cfg)) {}

    ~ConfigRestore() {
        vh::config::Registry::set(previous);
    }
};

struct ThreadPoolShutdown {
    bool active = false;

    ~ThreadPoolShutdown() {
        if (active)
            vh::concurrency::ThreadPoolManager::instance().shutdown();
    }
};

inline uint16_t freeLoopbackPort() {
    boost::asio::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
    return acceptor.local_endpoint().port();
}

inline http::response<http::string_body> httpGetRoot(const uint16_t port) {
    boost::asio::io_context ioc;
    tcp::socket socket(ioc);
    socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));

    timeval timeout{
        .tv_sec = 5,
        .tv_usec = 0
    };
    (void)::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)::setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    http::request<http::string_body> req{http::verb::get, "/", 11};
    req.set(http::field::host, "127.0.0.1:" + std::to_string(port));
    req.set(http::field::user_agent, "vaulthalla-s3-gateway-test");
    req.prepare_payload();

    http::write(socket, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(socket, buffer, res);
    return res;
}

inline std::string amzNow() {
    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&ts, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return out.str();
}

inline std::string amzAfter(const std::chrono::seconds offset) {
    const auto ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + offset);
    std::tm tm{};
    gmtime_r(&ts, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return out.str();
}

inline std::string scopeDate(const std::string& amzDate) {
    return amzDate.substr(0, 8);
}

inline void signS3GatewayRequest(
    vh::protocols::s3::Router::Request& request,
    const std::string& accessKey,
    const std::string& secretKey) {
    using namespace vh::protocols::s3::sigv4;

    const auto bodyHash = sha256Hex(request.body());
    const auto amzDate = amzNow();
    request.set("x-amz-content-sha256", bodyHash);
    request.set("x-amz-date", amzDate);

    VerificationInput input = inputFromRequest(request, request.body());
    ParsedAuth auth{
        .credential = {
            .access_key = accessKey,
            .date = scopeDate(amzDate),
            .region = "us-east-1",
            .service = "s3"
        },
        .signed_headers = "host;x-amz-content-sha256;x-amz-date",
        .signature = {},
        .amz_date = amzDate,
        .payload_hash = bodyHash
    };
    auth.signature = signatureFor(input, auth, secretKey);
    request.set(
        http::field::authorization,
        "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + auth.credential.date + "/us-east-1/s3/aws4_request, "
        "SignedHeaders=" + auth.signed_headers + ", Signature=" + auth.signature);
}

inline uint32_t ensureS3GatewayAdminRole(pqxx::work& txn, const vh::rbac::role::Admin& role) {
    return txn.exec(
        R"SQL(
            INSERT INTO admin_role (
                name,
                description,
                identity_permissions,
                audit_permissions,
                settings_permissions,
                roles_permissions,
                vaults_permissions,
                keys_permissions,
                s3_gateway_permissions
            )
            VALUES ($1, $2, $3::bit(32), $4::bit(8), $5::bit(64), $6::bit(16), $7::bit(32), $8::bit(32), $9::bit(8))
            ON CONFLICT (name) DO UPDATE SET
                description = EXCLUDED.description,
                identity_permissions = EXCLUDED.identity_permissions,
                audit_permissions = EXCLUDED.audit_permissions,
                settings_permissions = EXCLUDED.settings_permissions,
                roles_permissions = EXCLUDED.roles_permissions,
                vaults_permissions = EXCLUDED.vaults_permissions,
                keys_permissions = EXCLUDED.keys_permissions,
                s3_gateway_permissions = EXCLUDED.s3_gateway_permissions
            RETURNING id
        )SQL",
        pqxx::params{
            role.name,
            role.description,
            role.identities.toBitString(),
            role.audits.toBitString(),
            role.settings.toBitString(),
            role.roles.toBitString(),
            role.vaults.toBitString(),
            role.keys.toBitString(),
            role.s3Gateway.toBitString()
        }).one_field().as<uint32_t>();
}

inline uint32_t insertS3GatewayHydratableTestUser(
    pqxx::work& txn,
    const std::string& name,
    const std::string& email,
    const vh::rbac::role::Admin& role = vh::rbac::role::Admin::None()) {
    const auto userId = txn.exec(
        "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
        pqxx::params{name, email, "hash"}
    ).one_field().as<uint32_t>();
    const auto roleId = ensureS3GatewayAdminRole(txn, role);
    txn.exec(
        "INSERT INTO admin_role_assignments (user_id, role_id) VALUES ($1, $2)",
        pqxx::params{userId, roleId});
    return userId;
}

inline std::vector<uint8_t> hexBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

inline std::string uniqueS3Name(const std::string& label) {
    auto out = vh::vault::model::slugifyName(s3GatewayUniqueSuffix(label));
    if (out.size() > 63) out.resize(63);
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() < 3) out = "s3-" + out;
    return out;
}

class S3GatewayDbTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;
    inline static uint32_t userId = 0;
    inline static uint32_t vaultId = 0;

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
            std::cout << "[test_s3_gateway] Skipping db tests due to missing environment variables." << std::endl;
            return;
        }

        vh::paths::enableTestMode();
        const auto pathRoot = std::filesystem::temp_directory_path() / s3GatewayUniqueSuffix("vh_s3_gateway_db_paths");
        std::filesystem::remove_all(pathRoot);
        vh::paths::backingPath = pathRoot / "backing";
        vh::paths::mountPath = pathRoot / "mount";
        std::filesystem::create_directories(vh::paths::backingPath);
        std::filesystem::create_directories(vh::paths::mountPath);
        vh::db::Transactions::init();
        vh::db::seed::nuke_and_recreate_schema_public();
        vh::db::Transactions::dbPool_->initPreparedStatements();
        vh::seed::seed_database();

        vaultId = vh::db::Transactions::exec("S3GatewayDbTest::seed", [](pqxx::work& txn) {
            S3GatewayDbTest::userId = insertS3GatewayHydratableTestUser(
                txn,
                "s3_gateway_user",
                "s3-gateway@vaulthalla.test");

            const auto seededVaultId = txn.exec(
                "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ($1, $2, $3, $4, $5) RETURNING id",
                pqxx::params{"local", "S3 Gateway Test Vault", userId, "s3_gateway_test", ""}
            ).one_field().as<uint32_t>();
            txn.exec(
                "WITH ins AS (INSERT INTO sync (vault_id, interval) VALUES ($1, 300) RETURNING id) "
                "INSERT INTO fsync (sync_id, conflict_policy) SELECT id, 'keep_both' FROM ins",
                pqxx::params{seededVaultId});
            return seededVaultId;
        });
        vh::runtime::Deps::init();
        vh::fs::Filesystem::init(vh::runtime::Deps::get().storageManager);
        vh::runtime::Deps::get().storageManager->initStorageEngines();
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";
        vh::db::Transactions::exec("S3GatewayDbTest::clearObjects", [](pqxx::work& txn) {
            txn.exec("DELETE FROM s3_gateway_bucket");
            txn.exec("DELETE FROM s3_gateway_credentials");
            txn.exec("DELETE FROM s3_gateway_multipart_upload");
            txn.exec("DELETE FROM s3_gateway_object");
            txn.exec("DELETE FROM remote_object_index");
        });
        vh::runtime::Deps::get().storageManager->initStorageEngines();
    }

    static void putObject(const std::string& key) {
        vh::db::query::s3::Gateway::upsertObject({
            .vault_id = vaultId,
            .object_key = key,
            .etag = "\"etag-" + key + "\"",
            .size_bytes = 1,
            .content_type = "text/plain",
            .storage_class = std::nullopt,
            .last_modified = std::time(nullptr),
            .multipart = false,
            .part_count = std::nullopt
        });
    }

    static uint32_t createLocalVault(const std::string& label, const uint32_t ownerId) {
        const auto newVaultId = vh::db::Transactions::exec("S3GatewayDbTest::createLocalVault", [&](pqxx::work& txn) {
            const auto mountPoint = s3GatewayUniqueSuffix("s3gw");
            const auto seededVaultId = txn.exec(
                "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ($1, $2, $3, $4, $5) RETURNING id",
                pqxx::params{"local", "S3 Gateway " + label, ownerId, mountPoint.substr(0, 33), ""}
            ).one_field().as<uint32_t>();
            txn.exec(
                "WITH ins AS (INSERT INTO sync (vault_id, interval) VALUES ($1, 300) RETURNING id) "
                "INSERT INTO fsync (sync_id, conflict_policy) SELECT id, 'keep_both' FROM ins",
                pqxx::params{seededVaultId});
            return seededVaultId;
        });
        vh::runtime::Deps::get().storageManager->initStorageEngines();
        return newVaultId;
    }

    static uint32_t roleIdByName(const std::string& roleName) {
        return vh::db::Transactions::exec("S3GatewayDbTest::roleIdByName", [&](pqxx::work& txn) {
            const auto res = txn.exec(
                "SELECT id FROM vault_role WHERE name = $1 LIMIT 1",
                pqxx::params{roleName});
            if (res.empty()) throw std::runtime_error("vault role not found: " + roleName);
            return res.one_field().as<uint32_t>();
        });
    }

    static uint32_t userWithAdminRole(const std::string& label, const vh::rbac::role::Admin& role) {
        return vh::db::Transactions::exec("S3GatewayDbTest::userWithAdminRole", [&](pqxx::work& txn) {
            return insertS3GatewayHydratableTestUser(
                txn,
                "s3_gateway_" + label + "_" + s3GatewayUniqueSuffix("user"),
                "s3-gateway-" + label + "-" + s3GatewayUniqueSuffix("email") + "@vaulthalla.test",
                role);
        });
    }

    static vh::rbac::role::Admin adminRoleWithS3(
        const std::string& label,
        vh::rbac::permission::admin::S3Gateway s3Gateway,
        vh::rbac::permission::admin::Vaults vaults = vh::rbac::permission::admin::Vaults::None(),
        vh::rbac::permission::admin::Roles roles = vh::rbac::permission::admin::Roles::None()) {
        auto roleName = "s3gw_" + label;
        if (roleName.size() > 45) roleName.resize(45);
        return vh::rbac::role::Admin::Custom(
            roleName,
            "S3 gateway test role",
            vh::rbac::permission::admin::Identities::None(),
            std::move(vaults),
            vh::rbac::permission::admin::Audits::None(),
            vh::rbac::permission::admin::Settings::None(),
            std::move(roles),
            vh::rbac::permission::admin::Keys::None(),
            std::move(s3Gateway));
    }

    static std::shared_ptr<vh::protocols::ws::Session> wsSessionForUser(const uint32_t targetUserId) {
        auto session = std::make_shared<vh::protocols::ws::Session>(
            std::make_shared<vh::protocols::ws::Router>());
        session->user = vh::db::query::identities::User::getUserById(targetUserId);
        if (!session->user) throw std::runtime_error("test user not found");
        return session;
    }

    static void assignPrincipalVaultRole(
        const uint32_t targetVaultId,
        const uint32_t targetUserId,
        const std::string& roleName) {
        vh::db::Transactions::exec("S3GatewayDbTest::assignPrincipalVaultRole", [&](pqxx::work& txn) {
            const auto roleId = txn.exec(
                "SELECT id FROM vault_role WHERE name = $1 LIMIT 1",
                pqxx::params{roleName}).one_field().as<uint32_t>();
            txn.exec(
                "INSERT INTO vault_role_assignments (vault_id, subject_type, subject_id, role_id) "
                "VALUES ($1, 'user', $2, $3) "
                "ON CONFLICT (vault_id, subject_type, subject_id) DO UPDATE SET role_id = EXCLUDED.role_id",
                pqxx::params{targetVaultId, targetUserId, roleId});
        });
    }

    static vh::db::query::s3::GatewayCredential createCredential(
        const uint32_t principalUserId,
        const std::string& scopeMode) {
        vh::db::query::s3::GatewayCredential credential;
        credential.user_id = principalUserId;
        credential.principal_user_id = principalUserId;
        credential.created_by = principalUserId;
        credential.name = "s3gw-" + scopeMode + "-" + s3GatewayUniqueSuffix("credential");
        credential.access_key = "VHTEST" + s3GatewayUniqueSuffix("ACCESS").substr(0, 24);
        credential.encrypted_secret_access_key = {1, 2, 3};
        credential.iv = {4, 5, 6};
        credential.enabled = true;
        credential.scope_mode = scopeMode;
        credential.id = vh::db::query::s3::Gateway::createCredential(credential);
        return credential;
    }

    static void assignCredentialVaultRole(
        const uint32_t credentialId,
        const uint32_t targetVaultId,
        const std::string& roleName,
        const std::optional<uint32_t> createdBy = std::nullopt) {
        vh::db::query::s3::Gateway::upsertCredentialVaultRoleAssignment({
            .credential_id = credentialId,
            .vault_id = targetVaultId,
            .vault_role_id = roleIdByName(roleName),
            .enabled = true,
            .created_by = createdBy
        });
    }

    static void setCredentialDefaultVaultRole(
        const uint32_t credentialId,
        const std::string& roleName,
        const std::optional<uint32_t> createdBy = std::nullopt) {
        vh::db::query::s3::Gateway::upsertCredentialDefaultVaultRole(
            credentialId,
            roleIdByName(roleName),
            true,
            createdBy);
    }

    static void selectCredentialVault(
        const uint32_t credentialId,
        const uint32_t targetVaultId,
        const std::optional<uint32_t> createdBy = std::nullopt) {
        vh::db::query::s3::Gateway::upsertCredentialSelectedVault(
            credentialId,
            targetVaultId,
            true,
            createdBy);
    }

    static vh::rbac::permission::Override makeGatewayOverride(
        const std::string& permission,
        const std::string& pattern,
        const vh::rbac::permission::OverrideOpt effect) {
        vh::rbac::permission::Override out;
        out.permission.qualified_name = permission;
        out.effect = effect;
        out.enabled = true;
        out.pattern = vh::rbac::fs::glob::model::Pattern::make(pattern);
        return out;
    }

    static vh::rbac::s3::policy::Decision evaluateS3(
        const std::shared_ptr<vh::identities::User>& principal,
        const uint32_t credentialId,
        const std::string& scopeMode,
        const uint32_t targetVaultId,
        const vh::rbac::s3::policy::S3Action action,
        const std::filesystem::path& path = "/object.txt",
        const bool objectExists = true) {
        return vh::rbac::s3::policy::Evaluator::evaluate({
            .principal = principal,
            .credential_id = credentialId,
            .scope_mode = scopeMode,
            .vault_id = targetVaultId,
            .vault_path = path,
            .fuse_path = path,
            .action = action,
            .object_exists = objectExists,
            .is_directory_marker = path == std::filesystem::path{"/"},
            .target_user_id = std::nullopt
        });
    }
};

} // namespace vh::test::s3_gateway
