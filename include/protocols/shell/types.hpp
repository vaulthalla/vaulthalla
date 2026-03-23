#pragma once

#include "protocols/shell/SocketIO.hpp"

#include <functional>
#include <string>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <sstream>

namespace vh::identities { struct User; }

namespace vh::protocols::shell {

struct FlagKV {
    std::string key;
    std::optional<std::string> value;

    [[nodiscard]] std::string toString() const {
        if (value) return key + "=" + *value;
        return key;
    }
};

struct CommandCall {
    std::string name;
    std::vector<FlagKV> options;
    std::vector<std::string> positionals, original_positionals;
    bool rewrote = false;
    std::shared_ptr<identities::User> user;
    SocketIO* io = nullptr; // if set, command was run interactively

    // owns any strings you create at runtime (JSON, rewrites, etc.)
    std::vector<std::string> arena;

    [[nodiscard]] inline std::vector<std::string> constructFullArgs() const {
        if (name == "vh" && original_positionals.empty()) return {};
        std::vector<std::string> args;
        args.reserve(1 + original_positionals.size());
        args.push_back(name);
        for (const auto& pos : original_positionals) args.push_back(pos);
        return args;
    }

    [[nodiscard]] std::string toString() const {
        std::ostringstream oss;
        oss << "CommandCall:\n";
        const std::string in(2, ' ');
        oss << in << "Name: " << name << "\n";
        oss << in << "Options:\n";
        for (const auto& option : options) oss << std::string(4, ' ') << "  " << option.toString() << "\n";
        oss << in << "Positionals:\n";
        for (const auto& pos : positionals) oss << std::string(4, ' ') << "  " << pos << std::endl;
        oss << in << "Original Positionals:\n";
        for (const auto& pos : original_positionals) oss << std::string(4, ' ') << "  " << pos << std::endl;
        return oss.str();
    }
};

struct CommandResult {
    int exit_code = 0;                 // 0 = success
    std::string stdout_text;           // CLI stdout
    std::string stderr_text;           // CLI stderr
};

using CommandHandler = std::function<CommandResult(const CommandCall&)>;

struct CommandInfo {
    std::string description;                 // own
    CommandHandler handler;
    std::unordered_set<std::string> aliases; // own normalized aliases (no dashes)
    void print(std::string canonical) const;
};

}
