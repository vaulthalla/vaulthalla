#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/system/system_error.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace vh::protocols {

namespace asio  = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

[[noreturn]] inline void throw_with_context(std::string_view what, std::string_view detail) {
    throw std::runtime_error(std::string(what) + ": " + std::string(detail));
}

template <class Fn>
void wrap_sys(const std::string_view what, Fn&& fn) {
    try { std::forward<Fn>(fn)(); }
    catch (const boost::system::system_error& e) { throw_with_context(what, e.what()); }
}

inline std::string endpointToString(const tcp::acceptor& a) {
    const auto ep = a.local_endpoint();
    return ep.address().to_string() + ":" + std::to_string(ep.port());
}

inline void init_acceptor(tcp::acceptor& acceptor, const tcp::endpoint& endpoint) {
    wrap_sys("Failed to open acceptor", [&] { acceptor.open(endpoint.protocol()); });
    wrap_sys("Failed to set reuse_address", [&] {
        acceptor.set_option(asio::socket_base::reuse_address(true));
    });
    wrap_sys("Failed to bind acceptor", [&] { acceptor.bind(endpoint); });
    wrap_sys("Failed to listen on acceptor", [&] {
        acceptor.listen(asio::socket_base::max_listen_connections);
    });
}

}
