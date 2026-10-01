// Regression guards for the Phase 1 CLI/runtime-safety fixes:
//  - the shell server serves clients concurrently, answers every accepted connection promptly, and takes its
//    socket path back from a stale listener (a `vh` call used to hang forever behind one slow/idle client or an
//    orphaned vaulthalla-cli.socket listener);
//  - SocketIO never SIGPIPEs the daemon and bounds reads;
//  - secrets are redacted from ws debug logs; daemon-written secret files are 0600 and absolute-path only;
//  - (DB-backed) no CLI self-promotion, no role upsert-over-existing, no daemon crash on unknown group.

#include "auth/model/Token.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/session/Issuer.hpp"
#include "auth/session/Manager.hpp"
#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "crypto/util/hash.hpp"
#include "db/Transactions.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/rbac/role/admin/Assignments.hpp"
#include "identities/User.hpp"
#include "protocols/RoleGuards.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/Server.hpp"
#include "protocols/shell/SocketIO.hpp"
#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/ws/CookiePolicy.hpp"
#include "protocols/ws/DefaultPasswordGate.hpp"
#include "protocols/ws/LogRedaction.hpp"
#include "protocols/ws/ShareRateLimit.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/handler/Auth.hpp"
#include "protocols/ws/handler/rbac/roles/Admin.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "UsageManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vh::test_cli_runtime_safety {

using json = nlohmann::json;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

fs::path uniqueTempDir(const std::string& label) {
    const auto dir = fs::temp_directory_path() /
        (label + "_" + std::to_string(::getpid()) + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

void ensureUsageManager() {
    if (!runtime::Deps::get().shellUsageManager)
        runtime::Deps::get().shellUsageManager = std::make_shared<protocols::shell::UsageManager>();
}

// ---------------------------------------------------------------------------------------------------------------
// Minimal `vh` wire client

int connectTo(const fs::path& socketPath) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socketPath.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void sendRequest(const int fd, const std::string& line) {
    const json req{{"cmd", line.substr(0, line.find(' '))}, {"line", line}, {"interactive", false}};
    ASSERT_TRUE(protocols::shell::SocketIO::send_json(fd, req));
}

// Next frame within `timeout`, or nullopt (silence / closed).
std::optional<json> readFrame(const int fd, const std::chrono::milliseconds timeout) {
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    if (::poll(&pfd, 1, static_cast<int>(timeout.count())) <= 0) return std::nullopt;
    try {
        return protocols::shell::SocketIO::recv_json(fd);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool isGroupDenial(const json& frame) {
    return !frame.contains("type") && frame.value("exit_code", 0) == 77;
}

class ShellServerTest : public ::testing::Test {
protected:
    fs::path dir;
    fs::path socketPath;
    std::shared_ptr<protocols::shell::Server> server;

    void SetUp() override {
        paths::enableTestMode(); // never bootstrap the admin UID / run usermod from a test
        ensureUsageManager();
        dir = uniqueTempDir("vh_cli_server");
        socketPath = dir / "cli.sock";
        server = std::make_shared<protocols::shell::Server>();
        server->setSocketPath(socketPath.string());
        server->start();

        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!fs::exists(socketPath) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(20ms);
        ASSERT_TRUE(fs::exists(socketPath)) << "server never bound " << socketPath;
    }

    void TearDown() override {
        if (server) server->stop();
        server.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

TEST_F(ShellServerTest, IdleClientDoesNotBlockOtherClients) {
    // Client A connects and never sends its request. Before the fix the single-threaded accept loop sat in
    // read() on A and every other `vh` call hung behind it.
    const int idle = connectTo(socketPath);
    ASSERT_GE(idle, 0);

    const int active = connectTo(socketPath);
    ASSERT_GE(active, 0);
    sendRequest(active, "version");

    const auto first = readFrame(active, 3s);
    ASSERT_TRUE(first.has_value()) << "second client got no reply while the first was idle";
    if (!isGroupDenial(*first)) {
        EXPECT_EQ(first->value("type", ""), "hello");
        const auto result = readFrame(active, 5s);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->value("type", ""), "result");
        EXPECT_EQ(result->value("exit_code", -1), 0);
        EXPECT_NE(result->value("stdout", "").find("Vaulthalla v"), std::string::npos);
    }

    ::close(active);
    ::close(idle);
}

TEST_F(ShellServerTest, IdleClientIsDroppedAfterRequestTimeout) {
    const int idle = connectTo(socketPath);
    ASSERT_GE(idle, 0);

    // hello (or group denial) arrives immediately; then silence from us must end in a closed connection,
    // not a thread parked forever.
    ASSERT_TRUE(readFrame(idle, 3s).has_value());
    const auto deadline = protocols::shell::Server::kRequestTimeout + 5s;
    pollfd pfd{.fd = idle, .events = POLLIN, .revents = 0};
    ASSERT_GT(::poll(&pfd, 1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline).count())), 0);

    // Either an error frame followed by EOF, or EOF directly.
    for (int i = 0; i < 3; ++i) {
        char buf[4096];
        const auto n = ::recv(idle, buf, sizeof(buf), 0);
        if (n <= 0) break;
    }
    const auto startWait = std::chrono::steady_clock::now();
    while (server->activeClients() > 0 && std::chrono::steady_clock::now() - startWait < 3s)
        std::this_thread::sleep_for(50ms);
    EXPECT_EQ(server->activeClients(), 0u);
    ::close(idle);
}

TEST_F(ShellServerTest, RebindsWhenSocketPathIsTakenOver) {
    // Simulate systemd's vaulthalla-cli.socket (re)creating the path with a listener nobody accepts on.
    ::unlink(socketPath.c_str());
    const int stale = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(stale, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socketPath.c_str());
    ASSERT_EQ(::bind(stale, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(stale, 16), 0);

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (server->listenerRebinds() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(50ms);
    ASSERT_GE(server->listenerRebinds(), 1u) << "server never reclaimed its socket path";

    const int fd = connectTo(socketPath);
    ASSERT_GE(fd, 0);
    sendRequest(fd, "version");
    EXPECT_TRUE(readFrame(fd, 3s).has_value()) << "connection after rebind was not served";
    ::close(fd);
    ::close(stale);
}

TEST_F(ShellServerTest, StopUnlinksOnlyItsOwnSocket) {
    server->stop();
    EXPECT_FALSE(fs::exists(socketPath));
}

// ---------------------------------------------------------------------------------------------------------------

TEST(ShellSocketIO, SendToClosedPeerFailsWithoutSigpipe) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    ::close(sv[1]);
    // Would raise SIGPIPE (and kill the test binary, as it killed the daemon) with a plain write().
    EXPECT_FALSE(protocols::shell::SocketIO::send_json(sv[0], json{{"type", "output"}, {"text", std::string(1 << 16, 'x')}}));
    ::close(sv[0]);
}

TEST(ShellSocketIO, RecvTimesOutInsteadOfBlocking) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    timeval tv{.tv_sec = 0, .tv_usec = 200000};
    ASSERT_EQ(::setsockopt(sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW((void)protocols::shell::SocketIO::recv_json(sv[0]), std::runtime_error);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST(ShellSocketIO, RejectsOversizedFrame) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    const uint32_t len = htonl(protocols::shell::SocketIO::kMaxFrameBytes + 1);
    ASSERT_EQ(::send(sv[1], &len, 4, 0), 4);
    EXPECT_THROW((void)protocols::shell::SocketIO::recv_json(sv[0]), std::runtime_error);
    ::close(sv[0]);
    ::close(sv[1]);
}

// ---------------------------------------------------------------------------------------------------------------

TEST(WsLogRedaction, RedactsCredentialsAtAnyDepth) {
    const json msg{
        {"command", "auth.login"},
        {"token", "eyJhbGciOi"},
        {"payload", {
            {"name", "alice"},
            {"password", "hunter2"},
            {"nested", {{"secret_access_key", "AKIA-SECRET"}, {"refresh_token", "r"}}},
            {"keys", json::array({{{"api_key", "k1"}}})},
            {"new_password", nullptr}
        }}
    };
    const auto out = protocols::ws::redactForLog(msg);
    const auto dumped = out.dump();

    EXPECT_EQ(out["command"], "auth.login");
    EXPECT_EQ(out["payload"]["name"], "alice");
    EXPECT_EQ(out["token"], "[REDACTED]");
    EXPECT_EQ(out["payload"]["password"], "[REDACTED]");
    EXPECT_EQ(out["payload"]["nested"]["secret_access_key"], "[REDACTED]");
    EXPECT_EQ(out["payload"]["nested"]["refresh_token"], "[REDACTED]");
    EXPECT_EQ(out["payload"]["keys"][0]["api_key"], "[REDACTED]");
    EXPECT_TRUE(out["payload"]["new_password"].is_null());
    for (const auto* leaked : {"hunter2", "AKIA-SECRET", "eyJhbGciOi", "k1\""})
        EXPECT_EQ(dumped.find(leaked), std::string::npos) << leaked;
}

// ---------------------------------------------------------------------------------------------------------------

TEST(SecretOutputFiles, RequireAbsoluteRegularPath) {
    EXPECT_TRUE(protocols::shell::secretOutputPathError("keys.json").has_value());
    EXPECT_TRUE(protocols::shell::secretOutputPathError("./keys.json").has_value());
    EXPECT_TRUE(protocols::shell::secretOutputPathError("").has_value());

    const auto dir = uniqueTempDir("vh_secret_out");
    EXPECT_FALSE(protocols::shell::secretOutputPathError((dir / "keys.json").string()).has_value());

    const auto link = dir / "link.json";
    fs::create_symlink(dir / "target.json", link);
    EXPECT_TRUE(protocols::shell::secretOutputPathError(link.string()).has_value());
    EXPECT_THROW(protocols::shell::writePrivateFile(link.string(), "x"), std::runtime_error);
    EXPECT_FALSE(fs::exists(dir / "target.json"));
    fs::remove_all(dir);
}

TEST(SecretOutputFiles, WrittenOwnerOnlyEvenOverAnExistingLooseFile) {
    const auto dir = uniqueTempDir("vh_secret_mode");
    const auto path = dir / "keys.json";
    { std::ofstream(path) << "old"; }
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);

    protocols::shell::writePrivateFile(path.string(), "{\"secret\":1}");

    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
    std::ifstream in(path);
    const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(body, "{\"secret\":1}");
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------------------------------------------
// DB-backed: RBAC and crash regressions

class CliRbacDbTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;
    inline static std::shared_ptr<protocols::shell::Router> router;

    static bool hasDbEnv() {
        return std::getenv("VH_TEST_DB_USER") && std::getenv("VH_TEST_DB_PASS") && std::getenv("VH_TEST_DB_HOST") &&
               std::getenv("VH_TEST_DB_PORT") && std::getenv("VH_TEST_DB_NAME");
    }

    static void SetUpTestSuite() {
        if (!hasDbEnv()) {
            skipTests = true;
            std::cout << "[test_cli_runtime_safety] Skipping db tests due to missing environment variables." << std::endl;
            return;
        }

        paths::enableTestMode();
        db::Transactions::init();
        db::seed::nuke_and_recreate_schema_public();
        db::seed::init_tables_if_not_exists();
        db::Transactions::dbPool_->initPreparedStatements();
        seed::initPermissions();
        seed::initRoles();

        ensureUsageManager();
        router = std::make_shared<protocols::shell::Router>();
        protocols::shell::commands::registerUserCommands(router);
        protocols::shell::commands::registerGroupCommands(router);
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";
    }

    static std::string unique(const std::string& label) {
        return label + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000);
    }

    static std::shared_ptr<identities::User> createUser(const std::string& name, const std::string& roleName) {
        auto user = std::make_shared<identities::User>();
        user->name = name;
        user->email = name + "@vaulthalla.test";
        user->setPasswordHash("x");
        user->roles.admin = db::query::rbac::role::Admin::get(roleName);
        if (!user->roles.admin) throw std::runtime_error("Missing admin role: " + roleName);
        user->id = db::query::identities::User::createUser(user);
        return db::query::identities::User::getUserById(user->id);
    }

    // exit code + combined output; handler exceptions are what the shell server turns into exit 1.
    static std::pair<int, std::string> run(const std::string& line, const std::shared_ptr<identities::User>& user) {
        try {
            const auto res = router->executeLine(line, user, nullptr);
            return {res.exit_code, res.stdout_text + res.stderr_text};
        } catch (const std::exception& e) {
            return {1, e.what()};
        }
    }

    static std::string adminRoleBits(const std::string& name) {
        const auto role = db::query::rbac::role::Admin::get(name);
        if (!role) return "<missing>";
        return role->identities.toBitString() + role->vaults.toBitString() + role->roles.toBitString() +
               role->keys.toBitString() + role->settings.toBitString() + role->audits.toBitString() +
               role->s3Gateway.toBitString() + "|" + role->description;
    }

    static std::shared_ptr<protocols::ws::Session> sessionFor(const std::shared_ptr<identities::User>& user) {
        auto session = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
        session->user = user;
        return session;
    }
};

TEST_F(CliRbacDbTest, CannotChangeOwnRole) {
    const auto admin = createUser(unique("cli_admin_"), "admin");
    for (const auto& target : {std::string("super_admin"), std::string("unprivileged"),
                               std::to_string(db::query::rbac::role::Admin::get("super_admin")->id)}) {
        const auto [code, out] = run("user update " + admin->name + " --role " + target, admin);
        EXPECT_NE(code, 0) << "self role change to " << target << " succeeded: " << out;
    }
    EXPECT_EQ(db::query::identities::User::getUserById(admin->id)->roles.admin->name, "admin");
}

TEST_F(CliRbacDbTest, CannotAssignSuperAdminByRoleId) {
    const auto admin = createUser(unique("cli_admin_"), "admin");
    const auto plain = createUser(unique("cli_user_"), "unprivileged");
    const auto superId = db::query::rbac::role::Admin::get("super_admin")->id;

    const auto [code, out] = run("user update " + plain->name + " --role " + std::to_string(superId), admin);
    EXPECT_NE(code, 0) << out;
    EXPECT_FALSE(db::query::identities::User::getUserById(plain->id)->isSuperAdmin());
}

TEST_F(CliRbacDbTest, UnknownGroupIsAnErrorNotACrash) {
    const auto superUser = createUser(unique("cli_super_"), "super_admin");
    const std::vector<std::string> lines{
        "group info nosuch_group", "group update nosuch_group --description x", "group delete nosuch_group",
        "group user add nosuch_group " + superUser->name, "group user list nosuch_group", "group user list"};
    for (const auto& line : lines) {
        const auto [code, out] = run(line, superUser);
        EXPECT_NE(code, 0) << line;
    }
}

TEST_F(CliRbacDbTest, RoleCreateNeverOverwritesExistingRoles) {
    const auto before = adminRoleBits("super_admin");
    const auto adminBefore = adminRoleBits("admin");

    auto staged = std::make_shared<rbac::role::Admin>(rbac::role::Admin::None());
    staged->name = "super_admin";
    EXPECT_THROW((void)protocols::roles::createAdminRole(staged), protocols::roles::RoleAlreadyExists);
    staged->name = "admin";
    EXPECT_THROW((void)protocols::roles::createAdminRole(staged), protocols::roles::RoleAlreadyExists);

    // ws role.admin.add with an existing name: previously ON CONFLICT (name) DO UPDATE wiped its permissions.
    const auto superUser = createUser(unique("ws_super_"), "super_admin");
    const auto session = sessionFor(superUser);
    EXPECT_ANY_THROW((void)protocols::ws::handler::rbac::roles::Admin::add(
        json{{"name", "super_admin"}, {"description", "pwned"}}, session));
    EXPECT_ANY_THROW((void)protocols::ws::handler::rbac::roles::Admin::add(
        json{{"name", "admin"}, {"description", "pwned"}, {"id", db::query::rbac::role::Admin::get("admin")->id}}, session));

    EXPECT_EQ(adminRoleBits("super_admin"), before);
    EXPECT_EQ(adminRoleBits("admin"), adminBefore);
}

TEST_F(CliRbacDbTest, WsRoleCreateWithPermissionsWorks) {
    // role::Admin(json) used shared_from_this() in its constructor -> bad_weak_ptr on every web role create.
    json perms = json::array();
    for (const auto& p : rbac::role::Admin().toPermissions())
        perms.push_back({{"qualified", p.qualified_name}, {"value", false}});

    const auto superUser = createUser(unique("ws_super_"), "super_admin");
    const auto name = unique("ws_role_");
    const auto res = protocols::ws::handler::rbac::roles::Admin::add(
        json{{"name", name}, {"description", "custom"}, {"permissions", perms}}, sessionFor(superUser));
    EXPECT_TRUE(res.contains("role"));
    EXPECT_NE(db::query::rbac::role::Admin::get(name), nullptr);
}

TEST_F(CliRbacDbTest, BuiltInAndOwnRolesAreProtected) {
    const auto admin = createUser(unique("cli_admin_"), "admin");
    const auto superRole = db::query::rbac::role::Admin::get("super_admin");
    const auto adminRole = db::query::rbac::role::Admin::get("admin");
    const auto auditor = db::query::rbac::role::Admin::get("auditor");

    EXPECT_TRUE(protocols::roles::adminRoleUpdateError(*admin, *superRole, *superRole).has_value());
    EXPECT_TRUE(protocols::roles::adminRoleUpdateError(*admin, *adminRole, *adminRole).has_value()) << "own role";

    auto renamed = std::make_shared<rbac::role::Admin>(*auditor);
    renamed->name = "super_admin";
    EXPECT_TRUE(protocols::roles::adminRoleUpdateError(*admin, *auditor, *renamed).has_value());
    EXPECT_FALSE(protocols::roles::adminRoleUpdateError(*admin, *auditor, *auditor).has_value());

    EXPECT_TRUE(protocols::roles::adminRoleDeleteError(*admin, *superRole).has_value());
    EXPECT_TRUE(protocols::roles::adminRoleDeleteError(*admin, *adminRole).has_value());
}

// ---------------------------------------------------------------------------------------------------------------
// Issue #103: default admin password enforced server-side, and auth.login rate limited.

std::shared_ptr<protocols::ws::Session> closedSessionWith(const std::shared_ptr<protocols::ws::Router>& router,
                                                          const std::shared_ptr<identities::User>& user) {
    auto session = std::make_shared<protocols::ws::Session>(router);
    session->ipAddress = "203.0.113.7";
    session->user = user;
    session->close(); // responses become no-ops; we observe which handlers ran
    return session;
}

std::shared_ptr<identities::User> userWithPassword(const std::string& password) {
    auto user = std::make_shared<identities::User>();
    user->id = 4242;
    user->name = "admin";
    user->setPasswordHash(crypto::hash::password(password));
    return user;
}

// Installs a session manager and a test JWT secret so routed commands can pass real access-token validation
// (RequireHumanAuth) without a database; restores the previous runtime state on scope exit.
struct ScopedWsTokenAuth {
    std::shared_ptr<auth::session::Manager> previous;

    ScopedWsTokenAuth() : previous(runtime::Deps::get().sessionManager) {
        auth::session::Issuer::setJwtSecretForTesting("cli-runtime-safety-ws-auth-secret");
        runtime::Deps::get().sessionManager = std::make_shared<auth::session::Manager>();
    }

    ~ScopedWsTokenAuth() {
        runtime::Deps::get().sessionManager = previous;
        auth::session::Issuer::clearJwtSecretForTesting();
    }

    // A valid access token for `session`'s human user.
    static std::string issue(const std::shared_ptr<protocols::ws::Session>& session) {
        auth::session::Issuer::accessToken(session);
        return session->tokens->accessToken->rawToken;
    }
};

json routed(const std::string& command, const std::string& token = "") {
    return json{{"command", command}, {"payload", json::object()}, {"token", token}};
}

TEST(DefaultPasswordGate, OnlyPasswordChangeAndSessionCommandsWhileDefault) {
    // The gate is intentionally off in dev mode; pin production behavior regardless of the host's config.
    const config::Config previous = config::Registry::get(); // loads VH_PATH_TO_CONFIG on first use
    config::Config pinned = previous;
    pinned.dev.enabled = false;
    config::Registry::set(pinned);
    struct RestoreConfig { config::Config value; ~RestoreConfig() { config::Registry::set(value); } } restore{previous};

    // Account commands (auth.users.list, auth.user.change_password) need a valid access token since Stage 0 S1/S2;
    // give the session one so these exercise the default-password gate, not token validation.
    const ScopedWsTokenAuth tokenAuth;
    auto router = std::make_shared<protocols::ws::Router>();
    int blocked = 0, allowed = 0;
    router->registerHandler("auth.users.list", [&](json&&, const auto&) { ++blocked; });
    router->registerHandler("auth.user.change_password", [&](json&&, const auto&) { ++allowed; });
    router->registerHandler("auth.admin.default_password", [&](json&&, const auto&) { ++allowed; });

    const auto user = userWithPassword(std::string(protocols::ws::default_password::kSeededAdminPassword));
    const auto session = closedSessionWith(router, user);
    const auto token = ScopedWsTokenAuth::issue(session);
    EXPECT_TRUE(session->userHasDefaultPassword());

    router->routeMessage(routed("auth.users.list", token), session);
    router->routeMessage(routed("auth.user.change_password", token), session);
    router->routeMessage(json{{"command", "auth.admin.default_password"}, {"payload", nullptr}}, session);
    EXPECT_EQ(blocked, 0) << "a non-allowlisted command ran while the default password is still set";
    EXPECT_EQ(allowed, 2);

    // After a password change the session's user carries a new hash; the cached verdict is re-evaluated.
    user->setPasswordHash(crypto::hash::password("a-much-better-passphrase-123!"));
    EXPECT_FALSE(session->userHasDefaultPassword());
    router->routeMessage(routed("auth.users.list", token), session);
    EXPECT_EQ(blocked, 1);
}

TEST(DefaultPasswordGate, AllowlistIsMinimal) {
    using protocols::ws::default_password::isAllowedWhileDefault;
    for (const auto* cmd : {"auth.user.change_password", "auth.isAuthenticated", "auth.refresh", "auth.logout",
                            "auth.admin.default_password", "auth.login"})
        EXPECT_TRUE(isAllowedWhileDefault(cmd)) << cmd;
    for (const auto* cmd : {"auth.users.list", "auth.user.update", "auth.register", "role.admin.add",
                            "settings.update", "storage.vault.list", "fs.upload.start"})
        EXPECT_FALSE(isAllowedWhileDefault(cmd)) << cmd;
}

// ---------------------------------------------------------------------------------------------------------------
// Stage 0 S1/S2: `auth*` used to be routed as session-lifecycle for every session. Unauthenticated sockets reached
// account handlers that dereference session->user (a remote daemon segfault), and logged-in sessions reached
// them without access-token validation.

constexpr std::array<std::string_view, 5> kSessionLifecycleCommands{
    "auth.login", "auth.logout", "auth.refresh", "auth.isAuthenticated", "auth.admin.default_password"};
constexpr std::array<std::string_view, 8> kAccountCommands{
    "auth.register", "auth.user.delete", "auth.user.update", "auth.user.change_password",
    "auth.user.get", "auth.user.get.byName", "auth.users.list", "auth.user.anything_new"};

TEST(WsAuthRouting, OnlySessionLifecycleCommandsSkipHumanAuth) {
    using Decision = protocols::ws::Router::CommandAuthDecision;
    const auto unauth = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
    const auto human = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
    human->user = userWithPassword("irrelevant-for-routing");

    for (const auto cmd : kSessionLifecycleCommands) {
        EXPECT_EQ(Decision::Allow, protocols::ws::Router::classifyCommand(cmd, *unauth)) << cmd;
        EXPECT_EQ(Decision::Allow, protocols::ws::Router::classifyCommand(cmd, *human)) << cmd;
    }
    // Account commands take exactly the path of any ordinary authenticated command: RequireHumanAuth, i.e.
    // session::Manager::validate (access token, else the server-side refresh-token renewal) and the web client's
    // unauthorized -> refresh -> retry. Nothing auth-specific remains on that path.
    const auto ordinaryHuman = protocols::ws::Router::classifyCommand("storage.vault.list", *human);
    const auto ordinaryUnauth = protocols::ws::Router::classifyCommand("storage.vault.list", *unauth);
    for (const auto cmd : kAccountCommands) {
        EXPECT_EQ(Decision::Deny, protocols::ws::Router::classifyCommand(cmd, *unauth)) << cmd;
        EXPECT_EQ(Decision::RequireHumanAuth, protocols::ws::Router::classifyCommand(cmd, *human)) << cmd;
        EXPECT_EQ(ordinaryUnauth, protocols::ws::Router::classifyCommand(cmd, *unauth)) << cmd;
        EXPECT_EQ(ordinaryHuman, protocols::ws::Router::classifyCommand(cmd, *human)) << cmd;
    }
}

TEST(WsAuthRouting, UnauthenticatedSocketNeverReachesAccountHandlers) {
    auto router = std::make_shared<protocols::ws::Router>();
    int reached = 0;
    for (const auto cmd : kAccountCommands)
        router->registerHandler(std::string(cmd), [&](json&&, const auto&) { ++reached; });
    int lifecycle = 0;
    router->registerHandler("auth.admin.default_password", [&](json&&, const auto&) { ++lifecycle; });

    const auto session = std::make_shared<protocols::ws::Session>(router);
    session->ipAddress = "203.0.113.8";
    session->close();
    ASSERT_EQ(session->user, nullptr);

    for (const auto cmd : kAccountCommands) router->routeMessage(routed(std::string(cmd)), session);
    router->routeMessage(routed("auth.admin.default_password"), session);
    EXPECT_EQ(reached, 0) << "an account handler ran for an unauthenticated socket";
    EXPECT_EQ(lifecycle, 1);
}

TEST(WsAuthRouting, AccountCommandsRequireAValidAccessToken) {
    const ScopedWsTokenAuth tokenAuth;
    auto router = std::make_shared<protocols::ws::Router>();
    int reached = 0;
    router->registerHandler("auth.user.update", [&](json&&, const auto&) { ++reached; });

    auto user = userWithPassword("a-much-better-passphrase-123!");
    const auto session = closedSessionWith(router, user);
    const auto token = ScopedWsTokenAuth::issue(session);

    router->routeMessage(routed("auth.user.update", ""), session);
    router->routeMessage(routed("auth.user.update", token + "x"), session);
    EXPECT_EQ(reached, 0) << "auth.user.update ran without a valid access token";

    router->routeMessage(routed("auth.user.update", token), session);
    EXPECT_EQ(reached, 1);
}

TEST(WsAuthHandlers, NullSessionUserIsAnErrorNotACrash) {
    using protocols::ws::handler::Auth;
    const auto session = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
    session->close();
    ASSERT_EQ(session->user, nullptr);

    EXPECT_THROW((void)Auth::listUsers(session), std::exception);
    EXPECT_THROW((void)Auth::getUser(json{{"id", 1}}, session), std::exception);
    EXPECT_THROW((void)Auth::deleteUser(json{{"id", 1}}, session), std::exception);
    EXPECT_THROW((void)Auth::registerUser(json::object(), session), std::exception);
    EXPECT_THROW((void)Auth::updateUser(json::object(), session), std::exception);
    EXPECT_THROW((void)Auth::getUserByName(json{{"name", "admin"}}, session), std::exception);
}

TEST(LoginRateLimit, BurstThenSustainedLimitsPerIpAndAccount) {
    protocols::ws::ShareRateLimit limiter;
    const auto session = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
    session->ipAddress = "198.51.100.9";
    const json alice{{"command", "auth.login"}, {"payload", {{"name", "alice"}, {"password", "x"}}}};
    const json bob{{"command", "auth.login"}, {"payload", {{"name", "bob"}, {"password", "x"}}}};
    const auto t0 = protocols::ws::ShareRateLimit::Clock::now();

    // Successful logins never count: a script or several tabs logging in repeatedly is not guessing.
    for (int i = 0; i < 50; ++i) EXPECT_TRUE(limiter.check("auth.login", alice, *session, t0).allowed) << i;

    // Failed attempts do: 10 failures within a minute close the gate.
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.check("auth.login", alice, *session, t0).allowed) << i;
        limiter.recordLoginFailure("alice", *session, t0);
    }
    const auto denied = limiter.check("auth.login", alice, *session, t0);
    EXPECT_FALSE(denied.allowed);
    EXPECT_GT(denied.retry_after.count(), 0);
    // A shared proxy IP must not lock other accounts out.
    EXPECT_TRUE(limiter.check("auth.login", bob, *session, t0).allowed);

    // Keep failing at the per-minute cap: the 15-minute tier shuts the account/IP out for longer.
    for (int minute = 1; minute <= 3; ++minute)
        for (int i = 0; i < 10; ++i)
            if (limiter.check("auth.login", alice, *session, t0 + std::chrono::minutes(minute)).allowed)
                limiter.recordLoginFailure("alice", *session, t0 + std::chrono::minutes(minute));
    EXPECT_FALSE(limiter.check("auth.login", alice, *session, t0 + std::chrono::minutes(5)).allowed);
    EXPECT_TRUE(limiter.check("auth.login", alice, *session, t0 + std::chrono::minutes(20)).allowed);
}

}

// Session cookies are Secure only when the browser-facing request was HTTPS (behind the local proxy).
// Always-Secure made web login impossible on the package's default plain-HTTP nginx site.
TEST(WsCookiePolicy, SecureOnlyForHttpsSeenByTheLocalProxy) {
    using vh::protocols::ws::cookie_policy::isExternallyHttps;
    EXPECT_TRUE(isExternallyHttps("127.0.0.1", "https", ""));
    EXPECT_TRUE(isExternallyHttps("::1", "HTTPS", ""));
    EXPECT_TRUE(isExternallyHttps("::ffff:127.0.0.1", "https, http", ""));
    EXPECT_TRUE(isExternallyHttps("127.0.0.1", "", "for=10.0.0.11;proto=https;host=vault.example.com"));
    EXPECT_FALSE(isExternallyHttps("127.0.0.1", "http", ""));
    EXPECT_FALSE(isExternallyHttps("127.0.0.1", "", ""));
    EXPECT_FALSE(isExternallyHttps("127.0.0.1", "", "for=10.0.0.11;proto=http"));
    // A remote client talking to the daemon directly can't claim HTTPS.
    EXPECT_FALSE(isExternallyHttps("10.0.0.11", "https", ""));
    EXPECT_FALSE(isExternallyHttps("10.0.0.11", "", "proto=https"));
}
