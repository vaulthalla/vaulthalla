#pragma once

#include "config/Config.hpp"
#include "email/Provider.hpp"
#include "email/Transport.hpp"

#include <memory>

namespace vh::crypto::secrets { class Manager; }

namespace vh::email::providers {

class SesProvider final : public Provider {
public:
    static constexpr const char* kAccessKeySecret = "email.provider.ses.access_key_id";
    static constexpr const char* kSecretKeySecret = "email.provider.ses.secret_access_key";

    SesProvider(
        config::SesEmailConfig config,
        std::shared_ptr<crypto::secrets::Manager> secretsManager,
        std::unique_ptr<Transport> transport
    );

    [[nodiscard]] std::string name() const override { return "ses"; }
    SendResult send(const Message& message) override;

    [[nodiscard]] static std::string endpointForConfig(const config::SesEmailConfig& config);

private:
    config::SesEmailConfig config_;
    std::shared_ptr<crypto::secrets::Manager> secretsManager_;
    std::unique_ptr<Transport> transport_;
};

}
