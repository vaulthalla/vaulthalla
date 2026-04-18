#include "protocols/shell/Router.hpp"
#include "protocols/shell/Token.hpp"
#include "protocols/shell/Parser.hpp"
#include "log/Registry.hpp"
#include "identities/User.hpp"
#include "CommandUsage.hpp"
#include "protocols/shell/util/argsHelpers.hpp"

#include <fmt/core.h>
#include <cctype>
#include <string>
#include <algorithm>

using namespace vh::identities;

using namespace vh::protocols::shell;

void Router::registerCommand(const std::shared_ptr<CommandUsage>& usage, CommandHandler handler) {
    std::string key = normalize(usage->primary());

    CommandInfo info{usage->description.empty() ? "No description provided." : usage->description, std::move(handler), {}};

    for (const std::string& alias : usage->aliases) {
        std::string a = alias;
        if (aliasMap_.contains(a) && aliasMap_.at(a) != key) {
            log::Registry::shell()->warn("Alias '{}' already mapped to '{}'; skipping duplicate for '{}'",
                                       a, aliasMap_.at(a), key);
            continue;
        }
        info.aliases.insert(a);
        aliasMap_[a] = key;
        log::Registry::shell()->debug("Alias '{}' mapped to '{}'", a, key);
    }

    if (usage->pluralAliasImpliesList) pluralMap_[key + "s"] = key;

    commands_[key] = std::move(info);
}

std::string Router::canonicalFor(const std::string& nameOrAlias) const {
    std::string n = normalize(nameOrAlias);
    if (commands_.contains(n)) return n;
    if (aliasMap_.contains(n)) return aliasMap_.at(n);
    if (pluralMap_.contains(n)) return pluralMap_.at(n);
    return n; // unknown; let caller error
}

CommandResult Router::executeLine(const std::string& line, const std::shared_ptr<User>& user, SocketIO* io) const {
    log::Registry::shell()->debug("[Router] Executing line: '{}'", line);
    auto call   = parseTokens(tokenize(line));
    call.original_positionals = call.positionals;
    call.user = user;
    call.io = io;

    if (call.name.empty()) return invalid("No command provided.");
    const auto canonical = canonicalFor(call.name);

    if (call.positionals.empty() && pluralMap_.contains(call.name)) {
        call.name = canonical;
        call.positionals.emplace_back("list");
    }

    log::Registry::shell()->debug("[Router] Executing command: '{}'", canonical);

    if (!commands_.contains(canonical))
        return invalid(call.constructFullArgs(), fmt::format("[Router] Unknown command or alias: {}", call.name));

    return commands_.at(canonical).handler(call);
}

std::string Router::normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

std::string Router::strip_leading_dashes(const std::string& s) {
    size_t i = 0; while (i < s.size() && s[i] == '-') ++i;
    return std::string{s.substr(i)};
}

std::string Router::normalize_alias(const std::string& s) {
    return normalize(strip_leading_dashes(s));
}

std::string Router::joinAliases(const std::unordered_set<std::string>& aliases) {
    if (aliases.empty()) return "-";
    std::vector v(aliases.begin(), aliases.end());
    std::ranges::sort(v.begin(), v.end());
    std::string out;
    for (size_t i=0;i<v.size();++i) { if (i) out += ", "; out += v[i]; }
    return out;
}

std::string Router::pretty_alias(const std::string& a) {
    if (a == "?") return "?";
    if (a.size() == 1) return fmt::format("-{}", a);
    return fmt::format("--{}", a);
}
