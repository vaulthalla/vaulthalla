#include "config/Config.hpp"
#include "config/config_yaml.hpp"
#include "email/Message.hpp"
#include "email/Transport.hpp"
#include "email/providers/ResendProvider.hpp"
#include "email/templates/OperatorTemplates.hpp"
#include "usage/include/UsageManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <memory>

namespace {

vh::email::Message testMessage() {
    return {
        .from = {.email = "ops@example.com", .name = "Vaulthalla"},
        .to = {{.email = "admin@example.com", .name = std::nullopt}},
        .replyTo = vh::email::Address{.email = "reply@example.com", .name = std::nullopt},
        .subject = "[Vaulthalla] Test operator email",
        .html = "<strong>Hello</strong>",
        .text = "Hello",
        .idempotencyKey = "operator-test:unit",
        .tags = {}
    };
}

bool hasHeader(const vh::email::HttpRequest& request, const std::string& header) {
    return std::ranges::find(request.headers, header) != request.headers.end();
}

}

TEST(OperatorEmailConfigTest, JsonRoundTripUsesProviderStringsAndNoSecrets) {
    vh::config::Config cfg;
    cfg.email.enabled = true;
    cfg.email.provider = vh::config::EmailProviderKind::Resend;
    cfg.email.from = "Vaulthalla <ops@example.com>";
    cfg.email.reply_to = "ops@example.com";
    cfg.email.base_url = "https://vault.example.com";
    cfg.operator_emails.recipients.alerts = {"ops@example.com"};

    nlohmann::json encoded = cfg;

    EXPECT_EQ(encoded["email"]["provider"], "resend");
    EXPECT_EQ(encoded["email"]["reply_to"], "ops@example.com");
    EXPECT_FALSE(encoded.dump().contains("api_key"));
    EXPECT_FALSE(encoded.dump().contains("secret"));

    const auto decoded = encoded.get<vh::config::Config>();
    EXPECT_TRUE(decoded.email.enabled);
    EXPECT_EQ(decoded.email.provider, vh::config::EmailProviderKind::Resend);
    ASSERT_TRUE(decoded.email.base_url);
    EXPECT_EQ(*decoded.email.base_url, "https://vault.example.com");
    ASSERT_EQ(decoded.operator_emails.recipients.alerts.size(), 1u);
}

TEST(OperatorEmailConfigTest, YamlDecodeAcceptsNullOptionalFields) {
    const auto node = YAML::Load(R"yaml(
enabled: true
provider: resend
from: "Vaulthalla <ops@example.com>"
reply_to: null
base_url: null
resend:
  endpoint: "https://api.resend.test/emails"
)yaml");

    const auto cfg = node.as<vh::config::EmailConfig>();

    EXPECT_TRUE(cfg.enabled);
    EXPECT_EQ(cfg.provider, vh::config::EmailProviderKind::Resend);
    EXPECT_FALSE(cfg.reply_to);
    EXPECT_FALSE(cfg.base_url);
    EXPECT_EQ(cfg.resend.endpoint, "https://api.resend.test/emails");
}

TEST(OperatorEmailUsageTest, ResolvesPhaseOneCommandTree) {
    vh::protocols::shell::UsageManager manager;

    EXPECT_EQ(manager.resolve({"email"})->primary(), "email");
    EXPECT_EQ(manager.resolve({"email", "provider", "resend", "set"})->primary(), "set");
    EXPECT_EQ(manager.resolve({"email", "doctor"})->primary(), "doctor");
    EXPECT_EQ(manager.resolve({"email", "test", "--dry-run"})->primary(), "test");
}

TEST(OperatorEmailMessageTest, ParsesAndValidatesDisplayAddresses) {
    const auto address = vh::email::parseAddress("Vaulthalla <ops@example.com>");

    ASSERT_TRUE(address.name);
    EXPECT_EQ(*address.name, "Vaulthalla");
    EXPECT_EQ(address.email, "ops@example.com");
    EXPECT_EQ(vh::email::formatAddress(address), "Vaulthalla <ops@example.com>");
    EXPECT_NO_THROW(vh::email::validateMessage(testMessage()));
    EXPECT_THROW(vh::email::parseAddress("not-an-email"), std::invalid_argument);
}

TEST(OperatorEmailTemplateTest, RendersHtmlAndTextWithoutRawHtmlInjection) {
    const auto rendered = vh::email::templates::renderTestEmail({
        .provider = "resend",
        .instance = "host<prod>",
        .from = "Vaulthalla <ops@example.com>",
        .recipient = "admin+ops@example.com",
        .dryRun = true,
        .baseUrl = "https://vault.example.com"
    });

    EXPECT_EQ(rendered.subject, "[Vaulthalla] Test operator email");
    EXPECT_NE(rendered.html.find("host&lt;prod&gt;"), std::string::npos);
    EXPECT_EQ(rendered.html.find("host<prod>"), std::string::npos);
    EXPECT_NE(rendered.text.find("Provider: resend"), std::string::npos);
    EXPECT_FALSE(rendered.html.empty());
    EXPECT_FALSE(rendered.text.empty());
}

TEST(ResendProviderTest, SendsExpectedRequestThroughFakeTransport) {
    auto fake = std::make_unique<vh::email::FakeTransport>();
    fake->response = {.status = 202, .body = R"json({"id":"email_123"})json", .headers = ""};
    auto* fakePtr = fake.get();

    vh::email::providers::ResendProvider provider(
        {.endpoint = "https://api.resend.test/emails"},
        [] { return std::optional<std::string>{"re_unit_test_key"}; },
        std::move(fake)
    );

    const auto result = provider.send(testMessage());

    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.providerMessageId);
    EXPECT_EQ(*result.providerMessageId, "email_123");
    ASSERT_EQ(fakePtr->requests.size(), 1u);

    const auto& request = fakePtr->requests.front();
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.url, "https://api.resend.test/emails");
    EXPECT_TRUE(hasHeader(request, "Authorization: Bearer re_unit_test_key"));
    EXPECT_TRUE(hasHeader(request, "Content-Type: application/json"));
    EXPECT_TRUE(hasHeader(request, "Idempotency-Key: operator-test:unit"));

    const auto body = nlohmann::json::parse(request.body);
    EXPECT_EQ(body["from"], "Vaulthalla <ops@example.com>");
    EXPECT_EQ(body["to"][0], "admin@example.com");
    EXPECT_EQ(body["reply_to"], "reply@example.com");
    EXPECT_EQ(body["subject"], "[Vaulthalla] Test operator email");
    EXPECT_FALSE(request.body.contains("re_unit_test_key"));
}

TEST(ResendProviderTest, MissingSecretDoesNotCallTransport) {
    auto fake = std::make_unique<vh::email::FakeTransport>();
    auto* fakePtr = fake.get();

    vh::email::providers::ResendProvider provider(
        {},
        [] { return std::optional<std::string>{}; },
        std::move(fake)
    );

    const auto result = provider.send(testMessage());

    EXPECT_FALSE(result.ok);
    ASSERT_TRUE(result.errorSummary);
    EXPECT_EQ(*result.errorSummary, "missing Resend API key secret");
    EXPECT_TRUE(fakePtr->requests.empty());
}
