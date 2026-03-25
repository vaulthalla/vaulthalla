#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <iostream>

#include "../concurrency/TestCase.hpp"
#include "protocols/shell/types.hpp"
#include "log/Registry.hpp"
#include "fs/ops/file.hpp"
#include "../types/AssertionResult.hpp"

using namespace vh::fs::ops;

namespace vh::test::fuse {

// ------------------------------------------------------------
// Low-level execution: run lambda in a child with UID/GID, capture stdout
// ------------------------------------------------------------

struct ExecResult {
    int exit_code = -1;          // 0 on success, else (errno & 0xFF)
    std::string stdout_text;     // child stdout
};

inline ExecResult run_as_uid(const uid_t uid, const gid_t gid, const std::function<int()>& work_fn) {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        throw std::runtime_error(std::string("pipe2 failed: ") + std::strerror(errno));
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        // Child: redirect stdout to pipe
        ::close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            _exit((errno & 0xFF) ? (errno & 0xFF) : 1);
        }

        // Drop to requested creds (no /etc/passwd entry required)
        if (setresgid(gid, gid, gid) != 0) _exit(errno & 0xFF);
        if (setresuid(uid, uid, uid) != 0) _exit(errno & 0xFF);

        int rc = 0;
        try { rc = work_fn(); } catch (...) { rc = EFAULT; }
        _exit(rc & 0xFF);
    }

    // Parent: read child's stdout and wait
    ::close(pipefd[1]);
    ExecResult result;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(pipefd[0], buf, sizeof(buf))) > 0) {
        result.stdout_text.append(buf, buf + n);
    }
    ::close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        result.exit_code = (errno & 0xFF);
        return result;
    }
    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else                   result.exit_code = 255; // signaled

    return result;
}

// Convenience: same UID/GID
inline ExecResult run_as_uid(const uid_t uid, const std::function<int()>& work_fn) {
    return run_as_uid(uid, /*gid=*/uid, work_fn);
}

// ------------------------------------------------------------
// Basic FS ops (child prints minimal "OK ..." markers on success)
// ------------------------------------------------------------

inline int mkdirp(const std::filesystem::path& p, const mode_t mode = 0755) {
    (void)mode; // mode is ignored by create_directories
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec) return ec.value();
    std::cout << "OK mkdir " << p << "\n";
    return 0;
}

inline int write_file(const std::filesystem::path& p, const std::string_view data, const mode_t mode = 0644) {
    const int fd = ::open(p.c_str(), O_CREAT | O_TRUNC | O_WRONLY, mode);
    if (fd < 0) return errno;
    ssize_t w = ::write(fd, data.data(), data.size());
    const int rc = (w < 0) ? errno : 0;
    ::close(fd);
    if (rc == 0) std::cout << "OK write " << p << " bytes=" << data.size() << "\n";
    return rc;
}

inline int read_file(const std::filesystem::path& p) {
    log::Registry::vaulthalla()->warn("[read_file] reading {}", p.string());
    std::cout << "OK read " << p << ":\n" << std::flush;
    const int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return errno;
    char buf[4096];
    ssize_t r = ::read(fd, buf, sizeof(buf));
    const int rc = (r < 0) ? errno : 0;
    if (rc == 0 && r > 0)
        ::write(STDOUT_FILENO, buf, r);
    ::close(fd);
    return rc;
}

inline int list_dir(const std::filesystem::path& p) {
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(p, ec)) {
        if (ec) return ec.value();
        std::cout << e.path().filename().string() << "\n";
    }
    if (ec) return ec.value();
    return 0;
}

inline int rm_rf(const std::filesystem::path& p) {
    std::error_code ec;
    const auto count = std::filesystem::remove_all(p, ec);
    if (ec) return ec.value();
    std::cout << "OK rm -rf " << p << " removed=" << count << "\n";
    return 0;
}

inline int rename_path(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) return ec.value();
    std::cout << "OK mv " << from << " -> " << to << "\n";
    return 0;
}

inline int chmod_path(const std::filesystem::path& p, const mode_t mode) {
    if (::chmod(p.c_str(), mode) != 0) return errno;
    std::cout << "OK chmod " << p << " " << std::oct << mode << std::dec << "\n";
    return 0;
}

// ------------------------------------------------------------
// UID-scoped wrappers (return ExecResult)
// ------------------------------------------------------------

inline ExecResult mkdir_as(const uid_t uid, const std::filesystem::path& p, const mode_t mode = 0755) {
    return run_as_uid(uid, [=]{ return mkdirp(p, mode); });
}
inline ExecResult write_as(const uid_t uid, const std::filesystem::path& p, std::string_view const data, const mode_t mode = 0644) {
    return run_as_uid(uid, [=]{ return write_file(p, data, mode); });
}
inline ExecResult read_as(const uid_t uid, const std::filesystem::path& p) {
    return run_as_uid(uid, [=] { return read_file(p); });
}
inline ExecResult ls_as(const uid_t uid, const std::filesystem::path& p) {
    return run_as_uid(uid, [=]{ return list_dir(p); });
}
inline ExecResult rmrf_as(const uid_t uid, const std::filesystem::path& p) {
    return run_as_uid(uid, [=]{ return rm_rf(p); });
}
inline ExecResult mv_as(const uid_t uid, const std::filesystem::path& from, const std::filesystem::path& to) {
    return run_as_uid(uid, [=]{ return rename_path(from, to); });
}
inline ExecResult chmod_as(const uid_t uid, const std::filesystem::path& p, const mode_t mode) {
    return run_as_uid(uid, [=]{ return chmod_path(p, mode); });
}

// ------------------------------------------------------------
// Glue: build TestCases, run them, fill TestCase.result
// ------------------------------------------------------------

struct FuseStep {
    std::shared_ptr<vh::test::cli::TestCase> tc;
    std::function<ExecResult()> fn;
};

// Executes each step, maps ExecResult -> tc->result, and pre-fills assertion pass/fail
inline std::vector<std::shared_ptr<vh::test::cli::TestCase>>
run_fuse_steps(const std::vector<FuseStep>& steps) {
    using vh::test::cli::TestCase;
    std::vector<std::shared_ptr<TestCase>> out;
    out.reserve(steps.size());

    for (const auto& s : steps) {
        if (!s.tc || !s.fn) continue;
        const ExecResult exec = s.fn();

        // Map into TestCase::result
        s.tc->result.exit_code  = exec.exit_code;
        s.tc->result.stdout_text = exec.stdout_text;
        s.tc->result.stderr_text.clear(); // FUSE ops print only to stdout in our helpers

        // Pre-evaluate expectation (validateStage can still re-check if you prefer)
        bool ok = (s.tc->expect_exit == exec.exit_code);
        if (ok && !s.tc->must_contain.empty()) {
            for (const auto& needle : s.tc->must_contain) {
                if (s.tc->result.stdout_text.find(needle) == std::string::npos) { ok = false; break; }
            }
        }
        if (ok && !s.tc->must_not_contain.empty()) {
            for (const auto& bad : s.tc->must_not_contain) {
                if (s.tc->result.stdout_text.find(bad) != std::string::npos) { ok = false; break; }
            }
        }
        s.tc->assertion = ok ? cli::AssertionResult::Pass()
                             : cli::AssertionResult::Fail("FUSE: expectation mismatch (exit/stdout)");

        out.push_back(s.tc);
    }
    return out;
}

// Quick builder for a FUSE case (lets you set name/path/expected/matchers)
inline std::shared_ptr<vh::test::cli::TestCase>
make_fuse_case(std::string name, std::string path, int expect_exit,
               std::vector<std::string> must_contain = {},
               std::vector<std::string> must_not_contain = {}) {
    using vh::test::cli::TestCase;
    auto tc = std::make_shared<TestCase>();
    tc->name = std::move(name);
    tc->path = std::move(path);         // e.g. "fuse/mkdir"
    tc->expect_exit = expect_exit;      // 0 or EACCES, etc.
    tc->must_contain = std::move(must_contain);
    tc->must_not_contain = std::move(must_not_contain);
    return tc;
}

// Seeding helper (write a small tree as a specific UID, typically admin)
inline void seed_vault_tree(uid_t admin_uid, const std::filesystem::path& root, const std::string& base = "perm_seed") {
    (void)mkdir_as(admin_uid, root / base / "docs");
    (void)write_as(admin_uid, root / base / "docs" / "secret.txt", "TOP SECRET\n");
    (void)write_as(admin_uid, root / base / "note.txt", "hello\n");
}

} // namespace vh::test::fuse
