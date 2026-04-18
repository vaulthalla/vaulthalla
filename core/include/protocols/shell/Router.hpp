#pragma once

#include <protocols/shell/types.hpp>
#include "protocols/shell/SocketIO.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vh::identities { struct User; }

namespace vh::protocols::shell {

class CommandUsage;

class Router {
public:
    void registerCommand(const std::shared_ptr<CommandUsage>& usage, CommandHandler handler);

    CommandResult executeLine(const std::string& line,
        const std::shared_ptr<identities::User>& user,
        SocketIO* io = nullptr) const;

private:

    std::unordered_map<std::string, CommandInfo> commands_;
    std::unordered_map<std::string, std::string> aliasMap_; // alias -> canonical
    std::unordered_map<std::string, std::string> pluralMap_; // plural -> singular

    std::string canonicalFor(const std::string& nameOrAlias) const;

    static std::string normalize(const std::string& s);
    static std::string normalize_alias(const std::string& s);
    static std::string joinAliases(const std::unordered_set<std::string>& aliases);
    static std::string strip_leading_dashes(const std::string& s);
    static std::string pretty_alias(const std::string& a);
};

} // namespace vh::shell
