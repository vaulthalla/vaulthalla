#pragma once

#include <boost/beast/http.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <variant>

namespace vh::storage {
    class PlaintextReader;
}

namespace vh::protocols::http::model::preview {

namespace http = boost::beast::http;

// A body produced incrementally from a PlaintextReader: the session writes the header, then pulls bounded
// chunks (TCP backpressure via blocking writes with timeouts) and stops at the first write error, so a client
// that goes away stops the read/decrypt work. Content-Length must already be set to `length`.
struct StreamResponse : http::response_header<> {
    std::shared_ptr<storage::PlaintextReader> reader;  // null when there is no body (HEAD, 304, empty)
    uint64_t offset{};
    uint64_t length{};
    bool headOnly{false};
    bool omitContentLength{false};  // HEAD of a representation whose length is unknown without building it
    // Called once when the response ends: bytes of body sent and whether all `length` bytes went out.
    std::function<void(uint64_t sent, bool complete)> onFinish;
    bool keepAlive{true};

    // The subset of http::message the router and session use, so std::visit over Response stays uniform.
    void keep_alive(const bool value) { keepAlive = value; }
    [[nodiscard]] bool keep_alive() const { return keepAlive; }
};

using Response = std::variant<
    http::response<http::vector_body<uint8_t>>,
    http::response<http::file_body>,
    http::response<http::string_body>,
    StreamResponse
>;

}
