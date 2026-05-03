#pragma once

#include "protocols/http/model/preview/Response.hpp"

#include <boost/beast/http.hpp>
#include <boost/beast/http/file_body.hpp>
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace vh::share { class Manager; class TargetResolver; }
namespace vh::storage { struct Engine; }
namespace vh::protocols::ws { class Session; }

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
        using PreviewSessionResolver = std::function<std::shared_ptr<vh::protocols::ws::Session>(const request&)>;
        using SharePreviewManagerFactory = std::function<std::shared_ptr<share::Manager>()>;
        using SharePreviewResolverFactory = std::function<std::shared_ptr<share::TargetResolver>()>;
        using PreviewEngineResolver = std::function<std::shared_ptr<storage::Engine>(uint32_t)>;

        static model::preview::Response route(request &&req);

        static model::preview::Response handlePreview(request &&req);

        static model::preview::Response handleDownload(request &&req);

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

        static model::preview::Response makeDownloadResponse(const request &req,
                                                             std::vector<uint8_t> &&data,
                                                             const std::string &mime_type,
                                                             const std::string &filename);

        static std::string authenticateRequest(const request &req);

        static void setPreviewSessionResolverForTesting(PreviewSessionResolver resolver);
        static void resetPreviewSessionResolverForTesting();
        static void setSharePreviewManagerFactoryForTesting(SharePreviewManagerFactory factory);
        static void resetSharePreviewManagerFactoryForTesting();
        static void setSharePreviewResolverFactoryForTesting(SharePreviewResolverFactory factory);
        static void resetSharePreviewResolverFactoryForTesting();
        static void setPreviewEngineResolverForTesting(PreviewEngineResolver resolver);
        static void resetPreviewEngineResolverForTesting();
    };
}
