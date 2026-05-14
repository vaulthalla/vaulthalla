#pragma once

#include "email/RenderedEmail.hpp"

#include <optional>
#include <string>

namespace vh::email::templates {

struct TestEmailContext {
    std::string provider;
    std::string instance;
    std::string from;
    std::string recipient;
    bool dryRun = true;
    std::optional<std::string> baseUrl;
};

[[nodiscard]] std::string escapeHtml(std::string_view value);
[[nodiscard]] RenderedEmail renderTestEmail(const TestEmailContext& ctx);

}
