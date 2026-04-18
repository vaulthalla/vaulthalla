#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace vh::protocols::shell {

class CommandBook;
class CommandUsage;

class UsageManager {
public:
    UsageManager();
    ~UsageManager() = default;

    void registerBook(const std::shared_ptr<CommandBook>& book);

    std::shared_ptr<CommandUsage> resolve(const std::vector<std::string>& args) const;
    std::shared_ptr<CommandUsage> resolve(const std::string& topLevelArg) const;
    std::string renderHelp(const std::vector<std::string>& args) const;

    std::shared_ptr<CommandBook> bookFor(const std::string& topLevelAlias) const;

    [[nodiscard]] const std::shared_ptr<CommandUsage>& root() const { return root_; }

protected:
    std::unordered_map<std::string, std::shared_ptr<CommandBook>> index_;
    std::shared_ptr<CommandUsage> root_;
};

}
