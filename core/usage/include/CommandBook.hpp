#pragma once

#include "ColorTheme.hpp"

#include <string>
#include <optional>
#include <memory>
#include <vector>

namespace vh::protocols::shell {

class CommandUsage;

class CommandBook {
public:
    CommandBook() = default;
    ~CommandBook() = default;

    std::string title;
    std::optional<ColorTheme> book_theme;
    std::shared_ptr<CommandUsage> root;

    [[nodiscard]] std::string str() const;
    [[nodiscard]] std::string basicStr() const;
    [[nodiscard]] std::string markdown() const;

    [[nodiscard]] std::shared_ptr<CommandUsage> resolve(const std::vector<std::string>& args) const;
    [[nodiscard]] std::string renderHelp(const std::vector<std::string>& args) const;
};

}
