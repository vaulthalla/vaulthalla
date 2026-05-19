#include "crypto/secrets/Manager.hpp"
#include "email/Message.hpp"
#include "email/RenderedEmail.hpp"
#include "email/Transport.hpp"
#include "email/providers/SesProvider.hpp"
#include "email/templates/OperatorTemplates.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<std::string> sesLiveEnv(const char* key) {
    const auto* value = std::getenv(key);
    if (!value || *value == '\0') return std::nullopt;
    return std::string(value);
}

bool sesLiveFlagEnabled() {
    auto flag = sesLiveEnv("VH_TEST_AWS_SES_LIVE");
    if (!flag) return false;
    std::ranges::transform(*flag, flag->begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return *flag == "1" || *flag == "true" || *flag == "yes";
}

std::vector<std::string> sesLiveMissingRequiredEnv() {
    std::vector<std::string> missing;
    for (const auto* key : {
        "VH_TEST_AWS_SES_ACCESS_KEY",
        "VH_TEST_AWS_SES_SECRET_ACCESS_KEY",
        "VH_TEST_AWS_SES_FROM",
        "VH_TEST_AWS_SES_TO"
    }) {
        if (!sesLiveEnv(key)) missing.emplace_back(key);
    }
    return missing;
}

std::string sesLiveJoinKeys(const std::vector<std::string>& keys) {
    std::string out;
    for (const auto& key : keys) {
        if (!out.empty()) out += ", ";
        out += key;
    }
    return out;
}

vh::email::providers::SesProvider sesLiveProvider(
    const std::string& accessKey,
    const std::string& secretKey,
    const std::string& region
) {
    return vh::email::providers::SesProvider(
        {.region = region, .endpoint = std::nullopt},
        vh::crypto::secrets::Manager::createForTesting({
            {vh::email::providers::SesProvider::kAccessKeySecret, accessKey},
            {vh::email::providers::SesProvider::kSecretKeySecret, secretKey}
        }),
        std::make_unique<vh::email::CurlTransport>()
    );
}

vh::email::Message sesLivePreviewMessage(
    const vh::email::RenderedEmail& rendered,
    const std::string& from,
    const std::string& to,
    const std::string& label,
    const std::time_t now
) {
    return {
        .from = vh::email::parseAddress(from),
        .to = {vh::email::parseAddress(to)},
        .replyTo = std::nullopt,
        .subject = "[Vaulthalla Preview] " + rendered.subject,
        .html = rendered.html,
        .text = rendered.text,
        .idempotencyKey = "operator-email-preview:" + label + ":" + std::to_string(now),
        .tags = {{"event_type", "preview"}, {"provider", "ses"}, {"template", label}}
    };
}

void sesLiveSendPreview(
    vh::email::providers::SesProvider& provider,
    const vh::email::RenderedEmail& rendered,
    const std::string& from,
    const std::string& to,
    const std::string& label,
    const std::time_t now
) {
    SCOPED_TRACE(label);
    const auto result = provider.send(sesLivePreviewMessage(rendered, from, to, label, now));
    ASSERT_TRUE(result.ok) << (result.errorSummary ? *result.errorSummary : "SES preview send failed");
    EXPECT_GE(result.httpStatus, 200);
    EXPECT_LT(result.httpStatus, 300);
    EXPECT_TRUE(result.providerMessageId && !result.providerMessageId->empty());
}

vh::email::templates::WatchdogEmailContext sesLiveWatchdogAlertContext(const std::time_t now) {
    return {
        .instance = "prod-primary",
        .status = "critical",
        .severity = "critical",
        .fingerprint = "preview|critical|services:fuse,protocols:websocket",
        .checkedAt = static_cast<std::uint64_t>(now),
        .servicesReady = 11,
        .servicesTotal = 13,
        .depsReady = 9,
        .depsTotal = 10,
        .protocolsReady = 2,
        .protocolsTotal = 3,
        .failedServices = {"fuse mount service", "sync controller"},
        .missingDependencies = {"secrets manager"},
        .protocolIssues = {"websocket endpoint is not ready", "HTTP preview endpoint has elevated latency"},
        .baseUrl = "https://vault.example.com"
    };
}

vh::email::templates::WatchdogEmailContext sesLiveWatchdogRecoveryContext(const std::time_t now) {
    auto ctx = sesLiveWatchdogAlertContext(now);
    ctx.status = "healthy";
    ctx.severity = "info";
    ctx.checkedAt = static_cast<std::uint64_t>(now + 180);
    ctx.servicesReady = ctx.servicesTotal;
    ctx.depsReady = ctx.depsTotal;
    ctx.protocolsReady = ctx.protocolsTotal;
    ctx.failedServices.clear();
    ctx.missingDependencies.clear();
    ctx.protocolIssues.clear();
    return ctx;
}

vh::email::templates::WeeklyDigestEmailContext sesLiveWeeklyDigestContext(const std::time_t now) {
    return {
        .instance = "prod-primary",
        .weekStart = "2026-05-11",
        .weekEnd = "2026-05-17",
        .scheduledWeekday = "monday",
        .scheduledHourUtc = 8,
        .timezone = "UTC",
        .checkedAt = static_cast<std::uint64_t>(now),
        .systemStatus = "warning",
        .dashboardStatus = "warning",
        .warningCount = 5,
        .errorCount = 1,
        .servicesReady = 12,
        .servicesTotal = 13,
        .depsReady = 9,
        .depsTotal = 10,
        .protocolsReady = 3,
        .protocolsTotal = 3,
        .dashboardAvailable = true,
        .dashboardUnavailableReason = std::nullopt,
        .sections = {
            {.title = "Runtime", .severity = "warning", .summary = "One service required a restart during the window.", .warningCount = 1, .errorCount = 0},
            {.title = "Vaults", .severity = "healthy", .summary = "All vault sync loops are current.", .warningCount = 0, .errorCount = 0},
            {.title = "Sharing", .severity = "critical", .summary = "Email challenge retries were elevated for two links.", .warningCount = 2, .errorCount = 1},
            {.title = "Storage", .severity = "warning", .summary = "Filesystem metadata p95 latency crossed 250ms.", .warningCount = 2, .errorCount = 0}
        },
        .attention = {
            {.title = "Share challenge backlog", .severity = "critical", .message = "Review email validation retries before the next digest window."},
            {.title = "Filesystem latency", .severity = "warning", .message = "Metadata calls are slower than the operator threshold."},
            {.title = "Key rotation", .severity = "warning", .message = "One vault key rotation has been pending for more than 24 hours."}
        },
        .baseUrl = "https://vault.example.com"
    };
}

vh::email::templates::SecurityAlertEmailContext sesLiveSecurityAlertContext(const std::time_t now) {
    return {
        .instance = "prod-primary",
        .action = "updated",
        .severity = "warning",
        .occurredAt = static_cast<std::uint64_t>(now),
        .roleId = 42,
        .roleName = "security_admin",
        .roleDescription = "Security-focused administrative role for audits, identity lifecycle, and key visibility.",
        .actor = "alice (user id 7)",
        .source = "websocket",
        .permissionFlags = {
            "admin.audit.full",
            "admin.identities.admins.edit",
            "admin.identities.users.edit",
            "admin.keys.view",
            "admin.roles.admin.edit",
            "admin.settings.security.edit",
            "vault.global.manager"
        },
        .baseUrl = "https://vault.example.com"
    };
}

}

TEST(SesProviderLiveTest, SendsEmailWhenExplicitlyEnabled) {
    if (!sesLiveFlagEnabled())
        GTEST_SKIP() << "Set VH_TEST_AWS_SES_LIVE=1 to run SES live-fire email test.";

    const auto missing = sesLiveMissingRequiredEnv();
    if (!missing.empty())
        GTEST_SKIP() << "Skipping SES live-fire email test due to missing env keys: "
                     << sesLiveJoinKeys(missing);

    const auto accessKey = *sesLiveEnv("VH_TEST_AWS_SES_ACCESS_KEY");
    const auto secretKey = *sesLiveEnv("VH_TEST_AWS_SES_SECRET_ACCESS_KEY");
    const auto from = *sesLiveEnv("VH_TEST_AWS_SES_FROM");
    const auto to = *sesLiveEnv("VH_TEST_AWS_SES_TO");
    const auto region = sesLiveEnv("VH_TEST_AWS_SES_REGION").value_or("us-east-1");
    const auto now = std::time(nullptr);

    auto provider = sesLiveProvider(accessKey, secretKey, region);

    vh::email::Message message{
        .from = vh::email::parseAddress(from),
        .to = {vh::email::parseAddress(to)},
        .replyTo = std::nullopt,
        .subject = "[Vaulthalla] SES live test " + std::to_string(now),
        .html = "<p>This is an automated Vaulthalla SES provider live test.</p>",
        .text = "This is an automated Vaulthalla SES provider live test.",
        .idempotencyKey = "operator-email-ses-live:" + std::to_string(now),
        .tags = {{"event_type", "test"}, {"provider", "ses"}}
    };

    const auto result = provider.send(message);

    EXPECT_TRUE(result.ok) << (result.errorSummary ? *result.errorSummary : "SES send failed");
    EXPECT_GE(result.httpStatus, 200);
    EXPECT_LT(result.httpStatus, 300);
    EXPECT_TRUE(result.providerMessageId && !result.providerMessageId->empty());
}

TEST(SesProviderLiveTest, SendsRepresentativeOperatorTemplates) {
    if (!sesLiveFlagEnabled())
        GTEST_SKIP() << "Set VH_TEST_AWS_SES_LIVE=1 to run SES live-fire template preview test.";

    const auto missing = sesLiveMissingRequiredEnv();
    if (!missing.empty())
        GTEST_SKIP() << "Skipping SES live-fire template preview test due to missing env keys: "
                     << sesLiveJoinKeys(missing);

    const auto accessKey = *sesLiveEnv("VH_TEST_AWS_SES_ACCESS_KEY");
    const auto secretKey = *sesLiveEnv("VH_TEST_AWS_SES_SECRET_ACCESS_KEY");
    const auto from = *sesLiveEnv("VH_TEST_AWS_SES_FROM");
    const auto to = *sesLiveEnv("VH_TEST_AWS_SES_TO");
    const auto region = sesLiveEnv("VH_TEST_AWS_SES_REGION").value_or("us-east-1");
    const auto now = std::time(nullptr);
    auto provider = sesLiveProvider(accessKey, secretKey, region);

    sesLiveSendPreview(
        provider,
        vh::email::templates::renderWatchdogAlertEmail(sesLiveWatchdogAlertContext(now)),
        from,
        to,
        "watchdog-alert",
        now
    );
    sesLiveSendPreview(
        provider,
        vh::email::templates::renderWatchdogRecoveryEmail(sesLiveWatchdogRecoveryContext(now)),
        from,
        to,
        "watchdog-recovery",
        now
    );
    sesLiveSendPreview(
        provider,
        vh::email::templates::renderWeeklyDigestEmail(sesLiveWeeklyDigestContext(now)),
        from,
        to,
        "weekly-digest",
        now
    );
    sesLiveSendPreview(
        provider,
        vh::email::templates::renderSecurityAlertEmail(sesLiveSecurityAlertContext(now)),
        from,
        to,
        "security-alert",
        now
    );
}
