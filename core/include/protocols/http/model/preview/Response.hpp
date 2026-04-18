#pragma once

#include <boost/beast/http.hpp>

namespace vh::protocols::http::model::preview {

namespace http = boost::beast::http;

using Response = std::variant<
    http::response<http::vector_body<uint8_t>>,
    http::response<http::file_body>,
    http::response<http::string_body>
>;

}
