#include "protocols/shell/util/commandHelpers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace vh::protocols::shell::util {

std::string trim(std::string s) {
    const auto ws = [](const unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::ranges::find_if(s.begin(), s.end(), [&](const unsigned char c) { return !ws(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](const unsigned char c) { return !ws(c); }).base(), s.end());
    return s;
}

std::string shellQuote(const std::string& s) {
    std::string out{"'"};
    for (const char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

ExecResult runCapture(const std::string& command) {
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

    return {.code = code, .output = trim(output)};
}

bool commandExists(const std::string& command) {
    return runCapture("command -v " + shellQuote(command) + " >/dev/null").code == 0;
}

bool hasEffectiveRoot() {
    return ::geteuid() == 0;
}

bool canUsePasswordlessSudo() {
    if (!commandExists("sudo")) return false;
    return runCapture("sudo -n true").code == 0;
}

bool hasRootOrEquivalentPrivileges() {
    return hasEffectiveRoot() || canUsePasswordlessSudo();
}

std::string privilegedPrefix() {
    if (hasEffectiveRoot()) return "";
    if (canUsePasswordlessSudo()) return "sudo -n ";
    return "";
}

std::string formatFailure(const std::string& step, const ExecResult& result) {
    std::ostringstream msg;
    msg << step << " (exit " << result.code << ")";
    if (!result.output.empty()) msg << ": " << result.output;
    return msg.str();
}

}
