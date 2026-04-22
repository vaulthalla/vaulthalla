#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <cstring>
#include <fmt/core.h>
#include <algorithm>
#include <ranges>
#include <cctype>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cerrno>

static bool readn(const int fd, void* b, size_t n) {
    auto* p = static_cast<uint8_t*>(b);
    while (n) {
        const ssize_t r = ::read(fd, p, n);
        if (r <= 0) return false;
        p += r;
        n -= r;
    }
    return true;
}

static bool writen(const int fd, const void* b, size_t n) {
    const auto* p = static_cast<const uint8_t*>(b);
    while (n) {
        const ssize_t w = ::write(fd, p, n);
        if (w <= 0) return false;
        p += w;
        n -= w;
    }
    return true;
}

static void send_json_frame(const int fd, const nlohmann::json& j) {
    const auto s = j.dump();
    uint32_t n = htonl(static_cast<uint32_t>(s.size()));
    (void)writen(fd, &n, 4);
    (void)writen(fd, s.data(), s.size());
}

static bool recv_json_frame(const int fd, nlohmann::json& out) {
    uint32_t len_be = 0;
    if (!readn(fd, &len_be, 4)) return false;
    const uint32_t len = ntohl(len_be);
    std::string body(len, '\0');
    if (!readn(fd, body.data(), len)) return false;
    out = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    return !out.is_discarded();
}

static std::string ensureNewLine(const std::string& s) {
    if (s.empty() || s.back() != '\n') return s + '\n';
    return s;
}

static bool needs_quotes(std::string_view s) {
    if (s.empty()) return true;
    return std::ranges::any_of(s, [](const auto c) { return c == ' ' || c == '\t' || c == '"' || c == '\\'; });
}

static std::string dq_quote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (const char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// Heuristic: if tail has obvious "value" chars, treat "-Xtail" as glued value
static bool looks_glued_value(std::string_view tail) {
    if (tail.empty()) return false;
    return std::ranges::any_of(tail, [](const char c) {
        return c == '/' || c == '.' || c == ':' || c == '=';
    });
}

// Normalize argv tokens: split --key=value and -Xvalue (heuristic), keep -abc bundles as-is
static std::vector<std::string> normalize_tokens(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    out.reserve(in.size() + 4);
    for (auto a : in) {

        if (a == "--") { out.emplace_back("--"); continue; }

        if (a.rfind("--", 0) == 0) {
            const auto eq = a.find('=');
            if (eq != std::string::npos) {
                out.emplace_back(a.substr(0, eq));          // --key
                out.emplace_back(a.substr(eq + 1));         // value
            } else {
                out.emplace_back(std::move(a));
            }
            continue;
        }

        if (a.size() > 2 && a[0] == '-' && a[1] != '-') {
            // Could be -abc bundle OR -Xvalue
            std::string tail = a.substr(2);
            if (!tail.empty() && looks_glued_value(tail)) {
                out.emplace_back(std::string("-") + a[1]);  // -X
                if (tail[0] == '=') tail.erase(tail.begin());
                out.emplace_back(std::move(tail));          // value
            } else {
                out.emplace_back(std::move(a));             // keep bundle as-is
            }
            continue;
        }

        out.emplace_back(std::move(a)); // plain arg
    }
    return out;
}

static bool is_root_command_token(const std::string& token) {
    return token == "vh" || token == "vaulthalla" || token == "vaulthalla-cli";
}

static std::vector<std::string> canonicalize_argv_norm(const int argc, char** argv) {
    std::vector<std::string> raw;
    raw.reserve(argc > 1 ? static_cast<size_t>(argc) - 1 : 1);
    for (int i = 1; i < argc; ++i) {
        if (argv[i]) raw.emplace_back(argv[i]);
    }

    while (!raw.empty() && is_root_command_token(raw.front()))
        raw.erase(raw.begin());

    if (raw.empty()) raw.emplace_back("help");
    return normalize_tokens(raw);
}

static std::string build_line_from_tokens(const std::vector<std::string>& tokens) {
    std::string line;
    for (const auto& t : tokens) {
        if (!line.empty()) line.push_back(' ');
        line += needs_quotes(t) ? dq_quote(t) : t;
    }
    return line;
}

static bool has_flag(const std::vector<std::string>& argv_norm, std::string_view flag) {
    return std::ranges::any_of(argv_norm, [&](const std::string& a){ return a == flag; });
}

static bool is_interactive_allowed(const std::vector<std::string>& argv_norm) {
    // Hard opt-out via env or flags
    if (const char* env = std::getenv("VAULTHALLA_NONINTERACTIVE")) {
        if (std::string_view(env) == "1" || std::string_view(env) == "true") return false;
    }
    if (has_flag(argv_norm, "--non-interactive") || has_flag(argv_norm, "--yes"))
        return false;
    // Avoid interactivity if stdin isn't a TTY
    return ::isatty(STDIN_FILENO) == 1;
}

static std::string read_line_from_stdin() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

static bool command_is(const std::vector<std::string>& argv_norm, std::string_view cmd, std::string_view subcmd) {
    return argv_norm.size() >= 2 && argv_norm[0] == cmd && argv_norm[1] == subcmd;
}

static bool is_lifecycle_command(const std::vector<std::string>& argv_norm) {
    return command_is(argv_norm, "setup", "db")
        || command_is(argv_norm, "setup", "remote-db")
        || command_is(argv_norm, "setup", "nginx")
        || command_is(argv_norm, "teardown", "db")
        || command_is(argv_norm, "teardown", "nginx");
}

static int lifecycle_sudo_required(const std::vector<std::string>& argv_norm) {
    std::ostringstream out;
    out << "This lifecycle command must be run with elevated privileges.\n";
    out << "Re-run with sudo:\n";
    out << "  sudo vh";
    for (const auto& token : argv_norm)
        out << " " << (needs_quotes(token) ? dq_quote(token) : token);
    out << "\n";
    fmt::print(stderr, "{}", out.str());
    return 2;
}

static std::string lifecycle_binary_path() {
    if (const char* env = std::getenv("VAULTHALLA_LIFECYCLE_BIN"); env && *env)
        return env;
    return "/usr/lib/vaulthalla/lifecycle";
}

static int run_lifecycle_command(const std::vector<std::string>& argv_norm) {
    const auto lifecycleBin = lifecycle_binary_path();
    if (::access(lifecycleBin.c_str(), F_OK) != 0) {
        fmt::print(
            stderr,
            "Lifecycle utility not found: {}\n"
            "Install Vaulthalla lifecycle payload or set VAULTHALLA_LIFECYCLE_BIN to the utility path.\n",
            lifecycleBin
        );
        return 127;
    }
    if (::access(lifecycleBin.c_str(), X_OK) != 0) {
        fmt::print(
            stderr,
            "Lifecycle utility is not executable: {}\n"
            "Expected an executable script/binary. Reinstall Vaulthalla or fix file permissions.\n",
            lifecycleBin
        );
        return 126;
    }

    std::vector<std::string> args;
    args.reserve(argv_norm.size() + 1);
    args.push_back(lifecycleBin);
    args.insert(args.end(), argv_norm.begin(), argv_norm.end());

    std::vector<char*> execArgv;
    execArgv.reserve(args.size() + 1);
    for (auto& arg : args) execArgv.push_back(arg.data());
    execArgv.push_back(nullptr);

    ::execv(execArgv[0], execArgv.data());
    const auto err = errno;
    fmt::print(
        stderr,
        "Failed to execute lifecycle utility '{}': {}\n",
        lifecycleBin,
        std::strerror(err)
    );
    return err == ENOENT ? 127 : 126;
}

static bool should_append_status_systemd_summary(const std::vector<std::string>& argv_norm) {
    if (argv_norm.empty() || argv_norm[0] != "status") return false;
    if (::geteuid() != 0) return false;
    const auto hasHelpFlag = has_flag(argv_norm, "--help")
                          || has_flag(argv_norm, "--h")
                          || has_flag(argv_norm, "-h")
                          || has_flag(argv_norm, "?");
    return !hasHelpFlag;
}

static std::string shell_quote(const std::string& s) {
    std::string out{"'"};
    for (const char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static std::string trim_copy(std::string s) {
    const auto ws = [](const unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::ranges::find_if(s.begin(), s.end(), [&](const unsigned char c) { return !ws(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](const unsigned char c) { return !ws(c); }).base(), s.end());
    return s;
}

struct CaptureResult {
    int code = 0;
    std::string output;
};

static CaptureResult run_capture(const std::string& command) {
    const auto wrapped = command + " 2>&1";
    std::array<char, 4096> buf{};
    std::string output;

    FILE* pipe = ::popen(wrapped.c_str(), "r");
    if (!pipe) return {.code = 1, .output = "failed to execute command"};

    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
        output += buf.data();

    const int status = ::pclose(pipe);
    int code = status;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);

    return {.code = code, .output = output};
}

static bool command_exists(const std::string& command) {
    return run_capture("command -v " + shell_quote(command) + " >/dev/null").code == 0;
}

static std::string systemd_unit_state(const std::string& unit) {
    const auto state = run_capture("systemctl is-active " + shell_quote(unit));
    const auto trimmed = trim_copy(state.output);
    if (state.code == 0 && !trimmed.empty()) return trimmed;
    if (!trimmed.empty()) return trimmed;
    return "unknown (exit " + std::to_string(state.code) + ")";
}

static void append_status_systemd_summary_if_needed(const std::vector<std::string>& argv_norm, const int exitCode) {
    if (exitCode != 0 || !should_append_status_systemd_summary(argv_norm))
        return;
    if (!command_exists("systemctl")) return;

    std::ostringstream out;
    out << "systemd summary (supplemental):\n";
    constexpr std::array<std::string_view, 4> units{
        "vaulthalla.service",
        "vaulthalla-cli.service",
        "vaulthalla-cli.socket",
        "vaulthalla-web.service"
    };
    for (const auto unit : units)
        out << "  " << unit << ": " << systemd_unit_state(std::string(unit)) << "\n";

    fmt::print("{}", ensureNewLine(out.str()));
}

/* ----------------------------------------------------------------- */

int main(const int argc, char** argv) {
    std::vector<std::string> argv_norm = canonicalize_argv_norm(argc, argv);

    if (is_lifecycle_command(argv_norm)) {
        if (::geteuid() != 0)
            return lifecycle_sudo_required(argv_norm);
        return run_lifecycle_command(argv_norm);
    }

    // Quoted line for legacy servers
    std::string line = build_line_from_tokens(argv_norm);

    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "/run/vaulthalla/cli.sock");
    if (::connect(s, (sockaddr*)&addr, sizeof(sa_family_t) + std::strlen(addr.sun_path) + 1) != 0) {
        perror("connect");
        ::close(s);
        return 1;
    }

    // Prepare request
    const bool interactive = is_interactive_allowed(argv_norm);

    nlohmann::json j;
    j["cmd"]  = argv_norm.front();
    if (argv_norm.size() > 1) {
        std::vector<std::string> args(argv_norm.begin() + 1, argv_norm.end());
        j["args"] = args;
    } else {
        j["args"] = nlohmann::json::array();
    }
    j["argv"] = argv_norm;
    j["line"] = line;
    j["interactive"] = interactive;

    // Send request
    send_json_frame(s, j);

    // Frame loop: handle both streaming (type-based) and legacy single-reply
    for (;;) {
        nlohmann::json frame;
        if (!recv_json_frame(s, frame)) {
            // connection closed unexpectedly
            ::close(s);
            fmt::print(stderr, "{}", "Connection closed\n");
            return 1;
        }

        // Legacy reply: has no "type", but includes exit_code/ok/stdout/stderr
        if (!frame.contains("type")) {
            if (frame.contains("stdout")) fmt::print("{}", ensureNewLine(frame["stdout"].get<std::string>()));
            if (frame.contains("stderr")) fmt::print(stderr, "{}", ensureNewLine(frame["stderr"].get<std::string>()));
            const int ec = frame.value("exit_code", 0);
            append_status_systemd_summary_if_needed(argv_norm, ec);
            ::close(s);
            return ec;
        }

        const auto type = frame.value("type", std::string{});

        if (type == "output") {
            // {type:"output", text:"...", stream?: "stdout"|"stderr"}
            const auto text = frame.value("text", std::string{});
            const auto stream = frame.value("stream", std::string{"stdout"});
            if (stream == "stderr")
                fmt::print(stderr, "{}", ensureNewLine(text));
            else
                fmt::print("{}", ensureNewLine(text));
            continue;
        }

        if (type == "prompt") {
            // {type:"prompt", style:"confirm"|"input", id:"...", text:"...", default:"yes"|"no"|""}
            const auto id     = frame.value("id", std::string{});
            const auto style  = frame.value("style", std::string{"input"});
            const auto text   = frame.value("text", std::string{});
            const auto defval = frame.value("default", std::string{});

            // If we somehow got a prompt in non-interactive mode, bail cleanly
            if (!interactive) {
                fmt::print(stderr, "{}", "Interactive input requested but disabled. Re-run with --yes/flags.\n");
                ::close(s);
                return 1;
            }

            // Print the prompt exactly as sent
            if (!text.empty()) {
                // Don't double-append newline if server already included it
                if (!text.empty() && text.back() == '\n')
                    fmt::print("{}", text);
                else
                    fmt::print("{} ", text);
            }

            // Read one line from user
            std::string value = read_line_from_stdin();
            if (value.empty()) value = defval; // honor default on empty submit

            // Send response frame
            nlohmann::json reply{
                {"type","input"},
                {"id", id},
                {"value", value}
            };
            send_json_frame(s, reply);
            continue;
        }

        if (type == "result") {
            // {type:"result", ok:bool, exit_code:int, stdout?:..., stderr?:..., data?:...}
            if (frame.contains("stdout")) fmt::print("{}", ensureNewLine(frame["stdout"].get<std::string>()));
            if (frame.contains("stderr")) fmt::print(stderr, "{}", ensureNewLine(frame["stderr"].get<std::string>()));
            const int ec = frame.value("exit_code", 0);
            append_status_systemd_summary_if_needed(argv_norm, ec);
            ::close(s);
            return ec;
        }

        // Unknown frame type; print to stderr and continue
        fmt::print(stderr, "Unknown frame type: {}\n", type);
    }
}
