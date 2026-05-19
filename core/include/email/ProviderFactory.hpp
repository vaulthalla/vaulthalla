#pragma once

#include "email/Provider.hpp"
#include "email/Transport.hpp"
#include "config/Config.hpp"

#include <memory>

namespace vh::crypto::secrets { class Manager; }

namespace vh::email {

std::unique_ptr<Provider> makeProvider(
    const config::EmailConfig& config,
    std::shared_ptr<crypto::secrets::Manager> secretsManager,
    std::unique_ptr<Transport> transport = std::make_unique<CurlTransport>()
);

}
