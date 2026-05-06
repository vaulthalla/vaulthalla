#pragma once

#include "fs/ops/file.hpp"
#include "types/Type.hpp"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <grp.h>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>

using namespace vh::fs::ops;

namespace vh::test::integration::fuse {
    namespace detail {
        inline void append_text(std::string& out, const char* s) {
            if (s) out.append(s);
        }

        inline void append_path(std::string& out, const std::filesystem::path& p) {
            out.append(p.string());
        }

        inline void append_u32_oct(std::string& out, mode_t mode) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%o", static_cast<unsigned>(mode));
            out.append(buf);
        }

        inline void append_u64(std::string& out, std::uint64_t v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
            out.append(buf);
        }

        inline int write_all(const int fd, const char* data, const size_t len) {
            size_t off = 0;
            while (off < len) {
                const ssize_t w = ::write(fd, data + off, len - off);
                if (w < 0) {
                    if (errno == EINTR) continue;
                    return errno;
                }
                off += static_cast<size_t>(w);
            }
            return 0;
        }

        inline int write_all(const int fd, const std::string& s) {
            return write_all(fd, s.data(), s.size());
        }

        inline void emit_ok(const std::string& s) {
            (void)write_all(STDOUT_FILENO, s);
        }

        inline void close_if_valid(const int fd) {
            if (fd >= 0) ::close(fd);
        }
    }

    inline ExecResult run_as_uid(
        const uid_t uid,
        const gid_t gid,
        const std::function<int()>& work_fn,
        const int timeout_ms = 5000
    ) {
        int pipefd[2];
        if (pipe2(pipefd, O_CLOEXEC) != 0)
            throw std::runtime_error(std::string("pipe2 failed: ") + std::strerror(errno));

        const pid_t pid = ::fork();
        if (pid < 0) {
            detail::close_if_valid(pipefd[0]);
            detail::close_if_valid(pipefd[1]);
            throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
        }

        if (pid == 0) {
            detail::close_if_valid(pipefd[0]);

            if (::dup2(pipefd[1], STDOUT_FILENO) == -1)
                _exit((errno & 0xFF) ? (errno & 0xFF) : 1);

            if (::dup2(pipefd[1], STDERR_FILENO) == -1)
                _exit((errno & 0xFF) ? (errno & 0xFF) : 1);

            detail::close_if_valid(pipefd[1]);

            if (::geteuid() == 0) {
                if (::setgroups(0, nullptr) != 0)
                    _exit((errno & 0xFF) ? (errno & 0xFF) : 1);

                if (::setresgid(gid, gid, gid) != 0)
                    _exit((errno & 0xFF) ? (errno & 0xFF) : 1);

                if (::setresuid(uid, uid, uid) != 0)
                    _exit((errno & 0xFF) ? (errno & 0xFF) : 1);
            } else if (::geteuid() != uid || ::getegid() != gid) {
                _exit(EPERM);
            }

            int rc = 0;
            try {
                rc = work_fn();
            } catch (...) {
                rc = EFAULT;
            }

            _exit(rc & 0xFF);
        }

        detail::close_if_valid(pipefd[1]);

        ExecResult result;
        char buf[4096];

        pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN | POLLHUP;

        bool child_done = false;
        for (;;) {
            int status = 0;
            const pid_t w = ::waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                child_done = true;
                result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 255;
            } else if (w < 0) {
                result.exit_code = (errno & 0xFF) ? (errno & 0xFF) : 255;
                break;
            }

            const int prc = ::poll(&pfd, 1, timeout_ms);
            if (prc < 0) {
                if (errno == EINTR) continue;
                ::kill(pid, SIGKILL);
                (void)::waitpid(pid, nullptr, 0);
                detail::close_if_valid(pipefd[0]);
                result.exit_code = (errno & 0xFF) ? (errno & 0xFF) : 255;
                result.stderr_text = "poll() failed";
                return result;
            }

            if (prc == 0) {
                ::kill(pid, SIGKILL);
                (void)::waitpid(pid, nullptr, 0);
                detail::close_if_valid(pipefd[0]);
                result.exit_code = ETIMEDOUT;
                result.stderr_text = "run_as_uid timeout";
                return result;
            }

            if (pfd.revents & (POLLIN | POLLHUP)) {
                for (;;) {
                    const ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
                    if (n > 0) {
                        result.stdout_text.append(buf, static_cast<size_t>(n));
                        continue;
                    }
                    if (n == 0) break;
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    break;
                }
            }

            if (child_done && (pfd.revents & POLLHUP))
                break;
        }

        detail::close_if_valid(pipefd[0]);
        return result;
    }

    inline ExecResult run_as_user(
        const uid_t uid,
        const gid_t gid,
        const std::function<int()>& work_fn,
        const int timeout_ms = 5000
    ) {
        return run_as_uid(uid, gid, work_fn, timeout_ms);
    }

    inline int mkdirp(const std::filesystem::path& p, const mode_t mode = 0755) {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        if (ec) return ec.value();

        if (::chmod(p.c_str(), mode) != 0)
            return errno;

        std::string out = "OK mkdir ";
        detail::append_path(out, p);
        out.push_back('\n');
        detail::emit_ok(out);
        return 0;
    }

    inline int write_file(const std::filesystem::path& p, const std::string_view data, const mode_t mode = 0644) {
        const int fd = ::open(p.c_str(), O_CREAT | O_TRUNC | O_WRONLY, mode);
        if (fd < 0) return errno;

        const ssize_t w = ::write(fd, data.data(), data.size());
        const int saved = (w < 0) ? errno : 0;
        detail::close_if_valid(fd);
        if (saved != 0) return saved;

        std::string out = "OK write ";
        detail::append_path(out, p);
        detail::append_text(out, " bytes=");
        detail::append_u64(out, data.size());
        out.push_back('\n');
        detail::emit_ok(out);
        return 0;
    }

    inline int read_file(const std::filesystem::path& p) {
        std::string header = "OK read ";
        detail::append_path(header, p);
        detail::append_text(header, ":\n");
        detail::emit_ok(header);

        const int fd = ::open(p.c_str(), O_RDONLY);
        if (fd < 0) return errno;

        char buf[4096];
        for (;;) {
            const ssize_t r = ::read(fd, buf, sizeof(buf));
            if (r > 0) {
                const int rc = detail::write_all(STDOUT_FILENO, buf, static_cast<size_t>(r));
                if (rc != 0) {
                    detail::close_if_valid(fd);
                    return rc;
                }
                continue;
            }
            if (r == 0) break;
            if (errno == EINTR) continue;
            const int saved = errno;
            detail::close_if_valid(fd);
            return saved;
        }

        detail::close_if_valid(fd);
        return 0;
    }

    inline int list_dir(const std::filesystem::path& p) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(p, ec)) {
            if (ec) return ec.value();
            std::string line = e.path().filename().string();
            line.push_back('\n');
            detail::emit_ok(line);
        }
        if (ec) return ec.value();
        return 0;
    }

    inline int rm_rf(const std::filesystem::path& p) {
        std::error_code ec;
        const auto count = std::filesystem::remove_all(p, ec);
        if (ec) return ec.value();

        std::string out = "OK rm -rf ";
        detail::append_path(out, p);
        detail::append_text(out, " removed=");
        detail::append_u64(out, count);
        out.push_back('\n');
        detail::emit_ok(out);
        return 0;
    }

    inline int rename_path(const std::filesystem::path& from, const std::filesystem::path& to) {
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        if (ec) return ec.value();

        std::string out = "OK mv ";
        detail::append_path(out, from);
        detail::append_text(out, " -> ");
        detail::append_path(out, to);
        out.push_back('\n');
        detail::emit_ok(out);
        return 0;
    }

    inline int chmod_path(const std::filesystem::path& p, const mode_t mode) {
        if (::chmod(p.c_str(), mode) != 0)
            return errno;

        std::string out = "OK chmod ";
        detail::append_path(out, p);
        out.push_back(' ');
        detail::append_u32_oct(out, mode);
        out.push_back('\n');
        detail::emit_ok(out);
        return 0;
    }

    inline ExecResult mkdir_as(const uid_t uid, const gid_t gid, const std::filesystem::path& p, const mode_t mode = 0755) {
        return run_as_user(uid, gid, [=] { return mkdirp(p, mode); });
    }

    inline ExecResult write_as(const uid_t uid, const gid_t gid, const std::filesystem::path& p,
                               const std::string_view data, const mode_t mode = 0644) {
        return run_as_user(uid, gid, [=] { return write_file(p, data, mode); });
    }

    inline ExecResult read_as(const uid_t uid, const gid_t gid, const std::filesystem::path& p) {
        return run_as_user(uid, gid, [=] { return read_file(p); });
    }

    inline ExecResult ls_as(const uid_t uid, const gid_t gid, const std::filesystem::path& p) {
        return run_as_user(uid, gid, [=] { return list_dir(p); });
    }

    inline ExecResult rmrf_as(const uid_t uid, const gid_t gid, const std::filesystem::path& p) {
        return run_as_user(uid, gid, [=] { return rm_rf(p); });
    }

    inline ExecResult mv_as(const uid_t uid, const gid_t gid,
                            const std::filesystem::path& from, const std::filesystem::path& to) {
        return run_as_user(uid, gid, [=] { return rename_path(from, to); });
    }

    inline ExecResult chmod_as(const uid_t uid, const gid_t gid, const std::filesystem::path& p, const mode_t mode) {
        return run_as_user(uid, gid, [=] { return chmod_path(p, mode); });
    }

    // Back-compat overloads while you migrate callers.
    inline ExecResult mkdir_as(const uid_t uid, const std::filesystem::path& p, const mode_t mode = 0755) {
        return mkdir_as(uid, uid, p, mode);
    }

    inline ExecResult write_as(const uid_t uid, const std::filesystem::path& p,
                               const std::string_view data, const mode_t mode = 0644) {
        return write_as(uid, uid, p, data, mode);
    }

    inline ExecResult read_as(const uid_t uid, const std::filesystem::path& p) {
        return read_as(uid, uid, p);
    }

    inline ExecResult ls_as(const uid_t uid, const std::filesystem::path& p) {
        return ls_as(uid, uid, p);
    }

    inline ExecResult rmrf_as(const uid_t uid, const std::filesystem::path& p) {
        return rmrf_as(uid, uid, p);
    }

    inline ExecResult mv_as(const uid_t uid, const std::filesystem::path& from, const std::filesystem::path& to) {
        return mv_as(uid, uid, from, to);
    }

    inline ExecResult chmod_as(const uid_t uid, const std::filesystem::path& p, const mode_t mode) {
        return chmod_as(uid, uid, p, mode);
    }
}
