#include "protocols/http/handler/Handlers.hpp"

#include "config/Registry.hpp"
#include "fs/Filesystem.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "preview/Plan.hpp"
#include "protocols/http/Access.hpp"
#include "protocols/http/handler/Common.hpp"
#include "protocols/ws/Session.hpp"
#include "storage/Engine.hpp"
#include "storage/PlaintextReader.hpp"

#include <nlohmann/json.hpp>

namespace vh::protocols::http::handler {

namespace {

// Well-formed UTF-8 (no overlongs, surrogates or code points past U+10FFFF) without NUL bytes.
[[nodiscard]] bool isEditableText(const std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0) return false;
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            return false;
        }
        if (i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

}

model::preview::Response saveText(request&& req) {
    try {
        if (req.method() != verb::put) return jsonError(req, status::method_not_allowed, "invalid", "PUT required");
        const auto params = access::parseQuery(std::string(req.target()));
        const auto caller = access::authenticate(req, params);
        if (caller.share) return jsonError(req, status::forbidden, "denied", "Editing through share links is not supported");

        const auto target = access::resolve(caller, params, access::Need::Overwrite, access::Expect::File);
        if (preview::classify(*target.file).kind != preview::PreviewKind::TextDocument)
            return jsonError(req, status::unsupported_media_type, "unsupported", "Only text documents can be edited here");

        const auto limit = config::Registry::get().preview.text.max_edit_bytes;
        if (target.file->size_bytes > limit || req.body().size() > limit)
            return jsonError(req, status::payload_too_large, "limit_exceeded", "Document exceeds the editable size limit");
        if (!isEditableText(req.body()))
            return jsonError(req, status::unprocessable_entity, "invalid_input", "Content must be UTF-8 text without NUL bytes");

        const auto current = storage::generationOf(*target.file);
        const auto ifMatch = req.find(field::if_match);
        if (ifMatch == req.end())
            return jsonError(req, status::precondition_required, "precondition_required",
                             "Saving requires If-Match with the ETag the document was opened at");
        const std::string expected(ifMatch->value().data(), ifMatch->value().size());
        if (expected != current.etag()) {
            auto res = jsonError(req, status::precondition_failed, "conflict", "The document changed since you opened it");
            std::get<string_response>(res).set(field::etag, current.etag());
            return res;
        }

        // Normal encrypted reseal through the filesystem layer (fresh IV, atomic replace, DB + cache update,
        // thumbnail/derived invalidation by generation), conditional on the generation the editor opened.
        const auto& body = req.body();
        std::shared_ptr<fs::model::File> saved;
        try {
            saved = fs::Filesystem::createFile({
                .path = target.vaultPath,
                .fuse_path = target.engine->vaultPathToFusePath(target.vaultPath),
                .buffer = std::vector<uint8_t>(body.begin(), body.end()),
                .engine = target.engine,
                .user = caller.session->user,
                .overwrite = true,
                .expected_source_id = current.sourceId()
            });
        } catch (const fs::ContentConflict& e) {
            auto res = jsonError(req, status::precondition_failed, "conflict", e.what());
            std::get<string_response>(res).set(field::etag, storage::generationOf(*target.file).etag());
            return res;
        }
        if (!saved) throw std::runtime_error("Save produced no file");

        access::recordHumanAccess(caller, target, "text.save");
        const auto generation = storage::generationOf(*saved);
        auto res = Router::makeJsonResponse(req, {
            {"ok", true}, {"etag", generation.etag()}, {"size", saved->size_bytes}, {"updated_at", saved->updated_at}
        });
        std::get<string_response>(res).set(field::etag, generation.etag());
        std::get<string_response>(res).set(field::cache_control, "no-store");
        return res;
    } catch (const std::exception& e) {
        return errorFor(req, e);
    }
}

}
