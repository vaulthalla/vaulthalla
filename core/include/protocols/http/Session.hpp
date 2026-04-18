#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <memory>

namespace vh::protocols::http {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket);

    void run();

private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes);
    void on_write(bool close, beast::error_code ec, std::size_t bytes);
    void do_close();

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
};

}
