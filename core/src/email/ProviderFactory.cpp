#include "email/ProviderFactory.hpp"
#include "email/providers/ResendProvider.hpp"

#include <stdexcept>

namespace vh::email {

std::unique_ptr<Provider> makeProvider(
    const config::EmailConfig& config,
    std::shared_ptr<crypto::secrets::Manager> secretsManager,
    std::unique_ptr<Transport> transport
) {
    if (!transport) transport = std::make_unique<CurlTransport>();

    switch (config.provider) {
        case config::EmailProviderKind::Resend:
            return std::make_unique<providers::ResendProvider>(
                config.resend,
                std::move(secretsManager),
                std::move(transport)
            );
        case config::EmailProviderKind::None:
            return nullptr;
        case config::EmailProviderKind::Ses:
            throw std::runtime_error("SES email provider is planned for phase 2");
    }

    return nullptr;
}

}
