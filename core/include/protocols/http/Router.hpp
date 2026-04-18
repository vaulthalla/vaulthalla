#pragma once

#include "protocols/http/model/preview/Response.hpp"

#include <boost/beast/http.hpp>
#include <boost/beast/http/file_body.hpp>
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>

namespace vh::protocols::http {
    class Session;

    using request = boost::beast::http::request<boost::beast::http::string_body>;

    template<class Body>
    using response = boost::beast::http::response<Body>;

    using string_body = boost::beast::http::string_body;
    using file_body = boost::beast::http::file_body;
    using vector_body = boost::beast::http::vector_body<uint8_t>;;

    using field = boost::beast::http::field;
    using verb = boost::beast::http::verb;
    using status = boost::beast::http::status;

    using string_response = response<string_body>;
    using file_response = response<file_body>;
    using vector_response = response<vector_body>;

    struct Router {
        static model::preview::Response route(request &&req);

        static model::preview::Response handlePreview(request &&req);

        static model::preview::Response handleAuthSession(request &&req);

        static model::preview::Response makeResponse(const request &req,
                                                     std::vector<uint8_t> &&data,
                                                     const std::string &mime_type,
                                                     bool cacheHit = false);

        static model::preview::Response makeResponse(const request &req,
                                                     file_body::value_type data,
                                                     const std::string &mime_type,
                                                     bool cacheHit = false);

        static model::preview::Response makeErrorResponse(const request &req,
                                                          const std::string &msg,
                                                          const status &status = status::not_found);

        static model::preview::Response makeJsonResponse(const request &req,
                                                         const nlohmann::json &j);

        static std::string authenticateRequest(const request &req);
    };
}