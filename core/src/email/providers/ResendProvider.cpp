#include "email/providers/ResendProvider.hpp"

#include "crypto/secrets/Manager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace vh::email::providers {

namespace {

std::string safeSummary(const std::string& value) {
    constexpr std::size_t kMax = 300;
    std::string out = value;
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    if (out.size() > kMax) out = out.substr(0, kMax) + "...";
    return out;
}

std::string responseErrorSummary(const HttpResponse& response) {
    const auto parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (!parsed.is_discarded()) {
        if (parsed.contains("message") && parsed["message"].is_string())
            return safeSummary(parsed["message"].get<std::string>());
        if (parsed.contains("error") && parsed["error"].is_string())
            return safeSummary(parsed["error"].get<std::string>());
        if (parsed.contains("error") && parsed["error"].is_object() && parsed["error"].contains("message"))
            return safeSummary(parsed["error"]["message"].get<std::string>());
    }

    if (!response.body.empty()) return safeSummary(response.body);
    return "HTTP " + std::to_string(response.status);
}

}

ResendProvider::ResendProvider(
    config::ResendEmailConfig config,
    std::shared_ptr<crypto::secrets::Manager> secretsManager,
    std::unique_ptr<Transport> transport
) : config_(std::move(config)),
    secretsManager_(std::move(secretsManager)),
    transport_(std::move(transport)) {}

ResendProvider::ResendProvider(
    config::ResendEmailConfig config,
    SecretResolver secretResolver,
    std::unique_ptr<Transport> transport
) : config_(std::move(config)),
    secretResolver_(std::move(secretResolver)),
    transport_(std::move(transport)) {}

SendResult ResendProvider::send(const Message& message) {
    try {
        validateMessage(message);
    } catch (const std::exception& e) {
        SendResult result;
        result.errorSummary = e.what();
        return result;
    }

    std::optional<std::string> apiKey;
    if (secretResolver_) {
        apiKey = secretResolver_();
    } else if (secretsManager_) {
        apiKey = secretsManager_->getSecret(kApiKeySecret);
    } else {
        SendResult result;
        result.errorSummary = "secrets manager unavailable";
        return result;
    }

    if (!apiKey || apiKey->empty()) {
        SendResult result;
        result.errorSummary = "missing Resend API key secret";
        return result;
    }
    if (!transport_) {
        SendResult result;
        result.errorSummary = "email transport unavailable";
        return result;
    }

    nlohmann::json body{
        {"from", formatAddress(message.from)},
        {"to", nlohmann::json::array()},
        {"subject", message.subject},
        {"html", message.html},
        {"text", message.text}
    };

    for (const auto& recipient : message.to)
        body["to"].push_back(formatAddress(recipient));
    if (message.replyTo)
        body["reply_to"] = formatAddress(*message.replyTo);

    HttpRequest request;
    request.method = "POST";
    request.url = config_.endpoint;
    request.headers = {
        "Authorization: Bearer " + *apiKey,
        "Content-Type: application/json"
    };
    if (!message.idempotencyKey.empty())
        request.headers.push_back("Idempotency-Key: " + message.idempotencyKey);
    request.body = body.dump();

    const auto response = transport_->send(request);
    if (response.status < 200 || response.status >= 300) {
        SendResult result;
        result.errorSummary = responseErrorSummary(response);
        result.httpStatus = response.status;
        return result;
    }

    SendResult result;
    result.ok = true;
    result.httpStatus = response.status;
    const auto parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (!parsed.is_discarded() && parsed.contains("id") && parsed["id"].is_string())
        result.providerMessageId = parsed["id"].get<std::string>();
    return result;
}

}
