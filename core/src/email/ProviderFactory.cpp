#include "email/ProviderFactory.hpp"
#include "email/providers/ResendProvider.hpp"
#include "email/providers/SesProvider.hpp"

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
            return std::make_unique<providers::SesProvider>(
                config.ses,
                std::move(secretsManager),
                std::move(transport)
            );
    }

    return nullptr;
}

}
