#include "protocols/shell/Server.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/Parser.hpp"
#include "protocols/shell/commands/all.hpp"
#include "db/query/identities/User.hpp"
#include "log/Registry.hpp"
#include "protocols/shell/SocketIO.hpp"
#include "identities/User.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <grp.h>
#include <pwd.h>

using nlohmann::json;

using namespace vh::protocols::shell;

namespace {

struct Peer {
    uid_t uid;
    gid_t gid;
    pid_t pid;
};

Peer peercred(const int fd) {
    ucred c{};
    socklen_t len = sizeof(c);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &c, &len) != 0) throw std::runtime_error("SO_PEERCRED failed");
    return {c.uid, c.gid, c.pid};
}

bool in_group(uid_t uid, gid_t admin_gid) {
    if (uid == 0) return true;
    // Quick check: primary group ok?
    const passwd* pw = getpwuid(uid);
    if (!pw) return false;
    if (pw->pw_gid == admin_gid) return true;
    int ng = 0;
    getgrouplist(pw->pw_name, pw->pw_gid, nullptr, &ng);
    std::vector<gid_t> gs(ng);
    if (getgrouplist(pw->pw_name, pw->pw_gid, gs.data(), &ng) < 0) return false;
    return std::ranges::any_of(gs, [admin_gid](gid_t g) { return g == admin_gid; });
}

bool readn(const int fd, void* buf, size_t n) {
    auto* p = static_cast<unsigned char*>(buf);
    while (n) {
        const ssize_t r = ::read(fd, p, n);
        if (r <= 0) return false;
        p += r;
        n -= r;
    }
    return true;
}

bool writen(const int fd, const void* buf, size_t n) {
    auto* p = static_cast<const unsigned char*>(buf);
    while (n) {
        const ssize_t w = ::write(fd, p, n);
        if (w <= 0) return false;
        p += w;
        n -= w;
    }
    return true;
}

void send_json(const int fd, const json& j) {
    const auto s = j.dump();
    const auto len = htonl(static_cast<uint32_t>(s.size()));
    (void)writen(fd, &len, 4);
    (void)writen(fd, s.data(), s.size());
}

} // namespace



Server::Server()
    : AsyncService("vaulthalla-cli"),
      router_(std::make_shared<shell::Router>()),
      socketPath_("/run/vaulthalla/cli.sock"),
      adminGid_(getgrnam("vaulthalla")->gr_gid),
      adminUIDSet_(false) {

    commands::registerAllCommands(router_);

    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin) log::Registry::shell()->warn("[CtlServerService] No 'admin' user found in database");
    if (admin->meta.linux_uid.has_value()) adminUIDSet_.store(true);
}

Server::~Server() { closeListener(); }

void Server::closeActiveClient() {
    std::scoped_lock lock(fdMutex_);
    if (activeClientFd_ >= 0) {
        ::shutdown(activeClientFd_, SHUT_RDWR);
        ::close(activeClientFd_);
        activeClientFd_ = -1;
    }
}

void Server::closeClient(const int fd) {
    int fdToClose = -1;
    {
        std::scoped_lock lock(fdMutex_);
        if (activeClientFd_ == fd) {
            fdToClose = activeClientFd_;
            activeClientFd_ = -1;
        } else if (activeClientFd_ < 0 && shouldStop()) {
            // stop path already force-closed this client
            return;
        } else {
            // fallback cleanup for unexpected state
            fdToClose = fd;
        }
    }

    if (fdToClose >= 0) {
        ::shutdown(fdToClose, SHUT_RDWR);
        ::close(fdToClose);
    }
}

void Server::closeListener() {
    std::scoped_lock lock(fdMutex_);
    if (listenFd_ >= 0) {
        // shutdown() is the reliable unblock path for a thread blocked in accept4()
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    ::unlink(socketPath_.c_str());
}

void Server::onStop() {
    // Unblock any blocking client read/prompt first, then accept4().
    closeActiveClient();
    closeListener();
}

void Server::initAdminUid(const int cfd, const uid_t uid) {
    const auto msg = fmt::format(
        "[CtlServerService] 'admin' user has no Linux UID set; Assigning first user UID {} to 'admin'", uid);
    log::Registry::shell()->info(msg);
    log::Registry::audit()->info(msg);

    const struct passwd* pw = getpwuid(uid);
    if (!pw) throw std::runtime_error("Unable to resolve UID to username");
    const std::string uname = pw->pw_name;

    log::Registry::shell()->info("[CtlServerService] Adding UID {} to vaulthalla group", uid);

    // Assign UID to admin
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin) throw std::runtime_error("admin user disappeared");
    admin->meta.linux_uid = uid;
    db::query::identities::User::updateUser(admin);
    adminUIDSet_.store(true);

    log::Registry::shell()->info("[CtlServerService] Assigned UID {} to 'admin' user", uid);

    // Check if user is already in the vaulthalla group
    struct group* grp = getgrnam("vaulthalla");
    if (!grp) throw std::runtime_error("Group 'vaulthalla' not found");

    bool in_group = false;
    for (char** mem = grp->gr_mem; *mem != nullptr; ++mem) {
        if (uname == *mem) {
            in_group = true;
            break;
        }
    }

    if (!in_group) {
        // Fall back to system() since group modification isn't possible without `usermod`
        std::string cmd = fmt::format(kAddAdminCmd, uname);
        log::Registry::shell()->debug("[CtlServerService] Running fallback group add command: {}", cmd);

        if (const int result = std::system(cmd.c_str()); result != 0) {
            log::Registry::shell()->warn("[CtlServerService] usermod failed, checking group manually...");

            // Retry group check (maybe the change propagated anyway)
            endgrent(); // flush group cache
            grp = getgrnam("vaulthalla");
            if (!grp) throw std::runtime_error("Group 'vaulthalla' not found after fallback");

            in_group = false;
            for (char** mem = grp->gr_mem; *mem != nullptr; ++mem) {
                if (uname == *mem) {
                    in_group = true;
                    break;
                }
            }

            if (!in_group) {
                log::Registry::shell()->error("[CtlServerService] Failed to add '{}' to vaulthalla group", uname);
                send_json(cfd, {{"ok", false}, {"exit_code", 1}, {"stderr", "failed to add to vaulthalla group"}});
                throw std::runtime_error("Group add failed and could not verify fallback");
            }
        }
    }

    log::Registry::shell()->info("[CtlServerService] Verified '{}' in vaulthalla group", uname);
    log::Registry::audit()->info("Promoted user '{}' (UID {}) to admin group", uname, uid);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void Server::runLoop() {
    // Bind
    ::unlink(socketPath_.c_str());
    int listenFd = -1;
    {
        std::scoped_lock lock(fdMutex_);
        listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        listenFd = listenFd_;
    }
    if (listenFd < 0) throw std::runtime_error("socket()");
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socketPath_.c_str());
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr),
               sizeof(sa_family_t) + std::strlen(addr.sun_path) + 1) != 0)
        throw std::runtime_error("bind()");
    ::chmod(socketPath_.c_str(), 0660);
    // chown to vaulthalla:vaulthalla in your service startup if needed

    if (::listen(listenFd, 16) != 0) throw std::runtime_error("listen()");

    // Accept loop
    while (!shouldStop()) {
        int cfd = ::accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (shouldStop()) break;
            continue;
        }
        {
            std::scoped_lock lock(fdMutex_);
            activeClientFd_ = cfd;
        }

        try {
            const auto p = peercred(cfd);

            log::Registry::shell()->debug("[CtlServerService] Connection from UID {} (PID {})", p.uid, p.pid);

            if (!adminUIDSet_.load() && p.uid != 0 && p.uid >= 1000) initAdminUid(cfd, p.uid);

            if (!in_group(p.uid, adminGid_)) {
                log::Registry::shell()->debug("[CtlServerService] Connection from UID {} (PID {}) not in vaulthalla group",
                                           p.uid, p.pid);
                send_json(cfd, {{"ok", false}, {"exit_code", 1}, {"stderr", "permission denied"}});
                closeClient(cfd);
                continue;
            }

            const auto user = p.uid == 0 ?
            db::query::identities::User::getUserByName("system") :
            db::query::identities::User::getUserByLinuxUID(p.uid);

            if (!user) {
                log::Registry::shell()->debug("[CtlServerService] No user found for UID {} (PID {})", p.uid, p.pid);
                send_json(cfd, {{"ok", false}, {"exit_code", 1}, {"stderr", "user not found"}});
                closeClient(cfd);
                continue;
            }

            uint32_t be = 0;
            if (!readn(cfd, &be, 4)) {
                closeClient(cfd);
                continue;
            }
            const uint32_t len = ntohl(be);
            if (len > (1u << 20)) {
                // 1 MiB cap
                send_json(cfd, {{"ok", false}, {"exit_code", 1}, {"stderr", "request too large"}});
                closeClient(cfd);
                continue;
            }
            std::string body(len, '\0');
            if (!readn(cfd, body.data(), len)) {
                closeClient(cfd);
                continue;
            }

            json req = json::parse(body, nullptr, true);
            std::string cmd = req.value("cmd", "");
            std::vector<std::string> args = req.value("args", std::vector<std::string>{});
            if (cmd.empty()) cmd = "help";

            std::unique_ptr<SocketIO> io;

            auto try_exec_line = [&](const std::string& line) {
                try {
                    const auto res = router_->executeLine(line, user, io.get());

                    json reply{
                    {"type", "result"},
                    {"ok", res.exit_code == 0},
                    {"exit_code", res.exit_code}
                    };
                    if (!res.stdout_text.empty()) reply["stdout"] = res.stdout_text;
                    if (!res.stderr_text.empty()) reply["stderr"] = res.stderr_text;

                    SocketIO::send_json(cfd, reply);
                } catch (const std::exception& e) {
                    SocketIO::send_json(cfd, {
                        {"type", "result"},
                        {"ok", false},
                        {"exit_code", 1},
                        {"stderr", e.what()}
                    });
                }
            };

            if (req.contains("line") && req["line"].is_string()) {
                auto line = req["line"].get<std::string>();
                if (line.empty()) line = "help";
                if (req.value("interactive", false)) io = std::make_unique<SocketIO>(cfd);
                try_exec_line(line);
                closeClient(cfd);
                continue;
            }

            log::Registry::shell()->error("[CtlServerService] No 'line' field in request from UID {} (PID {})", p.uid,
                                        p.pid);
            send_json(cfd, {{"ok", false}, {"exit_code", 1}, {"stderr", "invalid request"}});
        } catch (const std::exception& e) {
            log::Registry::shell()->error("[CtlServerService] Internal error: {}", e.what());
            // best-effort error response
            send_json(cfd, {{"ok", false}, {"exit_code", 1}, {"stderr", e.what()}});
        } catch (...) {
            // best-effort error response
            send_json(cfd, {{"ok", false}, {"exit_code", 1}, {"stderr", "internal error"}});
        }
        closeClient(cfd);
    }
}
