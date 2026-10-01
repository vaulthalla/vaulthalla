#pragma once

#include <string_view>

namespace vh::protocols::ws::cookie_policy {

// Whether the browser-facing request was HTTPS, and therefore whether session cookies get `Secure`.
// The daemon itself speaks plain HTTP/ws, so TLS can only be terminated by a local reverse proxy (the
// packaged nginx site, `vh setup nginx`, or Caddy in dev). A forwarded scheme is trusted only when the
// peer is loopback; a direct remote client can't claim HTTPS. Browsers drop `Secure` cookies set for
// http:// origins, so marking them Secure over plain HTTP made web login impossible.
[[nodiscard]] bool isExternallyHttps(std::string_view peerAddress,
                                     std::string_view xForwardedProto,
                                     std::string_view forwarded);

[[nodiscard]] bool isLoopbackAddress(std::string_view address);

}
