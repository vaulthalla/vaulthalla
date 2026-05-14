#pragma once

#include "config/Config.hpp"
#include "email/Provider.hpp"
#include "email/Transport.hpp"

#include <memory>
#include <functional>
#include <optional>

namespace vh::crypto::secrets { class Manager; }

namespace vh::email::providers {

class ResendProvider final : public Provider {
public:
    static constexpr const char* kApiKeySecret = "email.provider.resend.api_key";
    using SecretResolver = std::function<std::optional<std::string>()>;

    ResendProvider(
        config::ResendEmailConfig config,
        std::shared_ptr<crypto::secrets::Manager> secretsManager,
        std::unique_ptr<Transport> transport
    );
    ResendProvider(
        config::ResendEmailConfig config,
        SecretResolver secretResolver,
        std::unique_ptr<Transport> transport
    );

    [[nodiscard]] std::string name() const override { return "resend"; }
    SendResult send(const Message& message) override;

private:
    config::ResendEmailConfig config_;
    std::shared_ptr<crypto::secrets::Manager> secretsManager_;
    SecretResolver secretResolver_;
    std::unique_ptr<Transport> transport_;
};

}
