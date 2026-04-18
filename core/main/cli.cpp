#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <cstring>
#include <fmt/core.h>
#include <algorithm>
#include <ranges>
#include <iostream>

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

// Normalize argv: split --key=value and -Xvalue (heuristic), keep -abc bundles as-is
static std::vector<std::string> normalize_args(const int argc, char** argv) {
    constexpr int start_index = 2; // skip argv[0] (program) and argv[1] (command)
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(argc) - start_index + 4);
    for (int i = start_index; i < argc; ++i) {
        std::string a = argv[i];

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

/* ----------------------------------------------------------------- */

int main(const int argc, char** argv) {
    // Decide the "command" token up front (safe even if argv[1] is nullptr)
    const std::string cmd = (argc >= 2 && argv[1] && argv[1][0] != '\0')
                            ? std::string(argv[1])
                            : std::string("help");

    // Build normalized argv: [cmd, args...], with --key=value and -Xvalue split
    std::vector<std::string> argv_norm;
    argv_norm.emplace_back(cmd);

    if (argc >= 3) {
        auto tail = normalize_args(argc, argv);
        argv_norm.insert(argv_norm.end(), tail.begin(), tail.end());
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
            ::close(s);
            return ec;
        }

        // Unknown frame type; print to stderr and continue
        fmt::print(stderr, "Unknown frame type: {}\n", type);
    }
}
