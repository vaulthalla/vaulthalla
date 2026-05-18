#pragma once

#include "email/RenderedEmail.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vh::email::templates {

struct TestEmailContext {
    std::string provider;
    std::string instance;
    std::string from;
    std::string recipient;
    bool dryRun = true;
    std::optional<std::string> baseUrl;
};

struct WatchdogEmailContext {
    std::string instance;
    std::string status;
    std::string severity;
    std::string fingerprint;
    std::uint64_t checkedAt = 0;
    std::size_t servicesReady = 0;
    std::size_t servicesTotal = 0;
    std::size_t depsReady = 0;
    std::size_t depsTotal = 0;
    std::size_t protocolsReady = 0;
    std::size_t protocolsTotal = 0;
    std::vector<std::string> failedServices;
    std::vector<std::string> missingDependencies;
    std::vector<std::string> protocolIssues;
    std::optional<std::string> baseUrl;
};

[[nodiscard]] std::string escapeHtml(std::string_view value);
[[nodiscard]] RenderedEmail renderTestEmail(const TestEmailContext& ctx);
[[nodiscard]] RenderedEmail renderWatchdogAlertEmail(const WatchdogEmailContext& ctx);
[[nodiscard]] RenderedEmail renderWatchdogRecoveryEmail(const WatchdogEmailContext& ctx);

}
