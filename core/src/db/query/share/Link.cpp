#include "db/query/share/Link.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/model/ListQueryParams.hpp"
#include "share/Link.hpp"
#include "share/Types.hpp"

#include <pqxx/pqxx>

#include <optional>
#include <regex>
#include <stdexcept>

namespace vh::db::query::share {

using vh::db::encoding::to_hex_bytea;

namespace link_query_detail {
bool is_uuid(const std::string& value) {
    static const std::regex pattern("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(value, pattern);
}

void require_uuid(const std::string& value, const char* name) {
    if (!is_uuid(value)) throw std::invalid_argument(std::string("Invalid share ") + name);
}

std::pair<uint64_t, uint64_t> page(const db::model::ListQueryParams& params) {
    const uint64_t limit = params.limit.value_or(100);
    const uint64_t page = std::max<uint64_t>(params.page.value_or(1), 1);
    return {limit, (page - 1) * limit};
}

std::optional<std::string> pg_array(const std::vector<std::string>& values) {
    if (values.empty()) return std::nullopt;
    std::string out = "{";
    bool first = true;
    for (const auto& value : values) {
        if (!first) out += ",";
        first = false;
        out += "\"";
        for (const char c : value) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\"";
    }
    out += "}";
    return out;
}

std::optional<std::string> pg_time(const std::optional<std::time_t>& value) {
    if (!value) return std::nullopt;
    return vh::db::encoding::timestampToString(*value);
}

std::vector<std::shared_ptr<vh::share::Link>> links_from_result(const pqxx::result& res) {
    std::vector<std::shared_ptr<vh::share::Link>> out;
    out.reserve(res.size());
    for (const auto& row : res) out.push_back(std::make_shared<vh::share::Link>(row));
    return out;
}

void require_link(const std::shared_ptr<vh::share::Link>& link) {
    if (!link) throw std::invalid_argument("Share link is required");
    if (link->token_lookup_id.empty()) throw std::invalid_argument("Share token lookup id is required");
    if (link->token_hash.empty()) throw std::invalid_argument("Share token hash is required");
    if (link->created_by == 0) throw std::invalid_argument("Share creator is required");
    if (link->vault_id == 0) throw std::invalid_argument("Share vault is required");
    if (link->root_entry_id == 0) throw std::invalid_argument("Share root entry is required");
}

void expect_row(const pqxx::result& res, const char* operation) {
    if (res.empty()) throw std::runtime_error(std::string("Share link mutation affected no rows: ") + operation);
}
}

std::shared_ptr<vh::share::Link> Link::create(const std::shared_ptr<vh::share::Link>& link) {
    link_query_detail::require_link(link);
    return Transactions::exec("share::Link::create", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Link> {
        pqxx::params p{
            link->token_lookup_id,
            to_hex_bytea(link->token_hash),
            link->created_by,
            link->vault_id,
            link->root_entry_id,
            link->root_path,
            vh::share::to_string(link->target_type),
            vh::share::to_string(link->link_type),
            vh::share::to_string(link->access_mode),
            link->allowed_ops,
            link->name,
            link->public_label,
            link->description,
            link_query_detail::pg_time(link->expires_at),
            link->max_downloads,
            link->max_upload_files,
            link->max_upload_size_bytes,
            link->max_upload_total_bytes,
            vh::share::to_string(link->duplicate_policy),
            link_query_detail::pg_array(link->allowed_mime_types),
            link_query_detail::pg_array(link->blocked_mime_types),
            link_query_detail::pg_array(link->allowed_extensions),
            link_query_detail::pg_array(link->blocked_extensions),
            link->metadata.empty() ? "{}" : link->metadata
        };
        const auto res = txn.exec(pqxx::prepped{"share_link_insert"}, p);
        return std::make_shared<vh::share::Link>(res.one_row());
    });
}

std::shared_ptr<vh::share::Link> Link::get(const std::string& id) {
    link_query_detail::require_uuid(id, "id");
    return Transactions::exec("share::Link::get", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Link> {
        const auto res = txn.exec(pqxx::prepped{"share_link_get"}, id);
        if (res.empty()) return nullptr;
        return std::make_shared<vh::share::Link>(res.one_row());
    });
}

std::shared_ptr<vh::share::Link> Link::getByLookupId(const std::string& lookup_id) {
    link_query_detail::require_uuid(lookup_id, "lookup id");
    return Transactions::exec("share::Link::getByLookupId", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Link> {
        const auto res = txn.exec(pqxx::prepped{"share_link_get_by_lookup_id"}, lookup_id);
        if (res.empty()) return nullptr;
        return std::make_shared<vh::share::Link>(res.one_row());
    });
}

std::vector<std::shared_ptr<vh::share::Link>> Link::listForUser(const uint32_t user_id, const db::model::ListQueryParams& params) {
    auto [limit, offset] = link_query_detail::page(params);
    return Transactions::exec("share::Link::listForUser", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(user_id);
        p.append(static_cast<long long>(limit));
        p.append(static_cast<long long>(offset));
        return link_query_detail::links_from_result(txn.exec(pqxx::prepped{"share_link_list_for_user"}, p));
    });
}

std::vector<std::shared_ptr<vh::share::Link>> Link::listForVault(const uint32_t vault_id, const db::model::ListQueryParams& params) {
    auto [limit, offset] = link_query_detail::page(params);
    return Transactions::exec("share::Link::listForVault", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(vault_id);
        p.append(static_cast<long long>(limit));
        p.append(static_cast<long long>(offset));
        return link_query_detail::links_from_result(txn.exec(pqxx::prepped{"share_link_list_for_vault"}, p));
    });
}

std::vector<std::shared_ptr<vh::share::Link>> Link::listForTarget(const uint32_t root_entry_id, const db::model::ListQueryParams& params) {
    auto [limit, offset] = link_query_detail::page(params);
    return Transactions::exec("share::Link::listForTarget", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(root_entry_id);
        p.append(static_cast<long long>(limit));
        p.append(static_cast<long long>(offset));
        return link_query_detail::links_from_result(txn.exec(pqxx::prepped{"share_link_list_for_target"}, p));
    });
}

std::shared_ptr<vh::share::Link> Link::update(const std::shared_ptr<vh::share::Link>& link) {
    link_query_detail::require_link(link);
    link_query_detail::require_uuid(link->id, "id");
    return Transactions::exec("share::Link::update", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Link> {
        pqxx::params p{
            link->id,
            link->updated_by,
            vh::share::to_string(link->target_type),
            vh::share::to_string(link->link_type),
            vh::share::to_string(link->access_mode),
            link->allowed_ops,
            link->name,
            link->public_label,
            link->description,
            link_query_detail::pg_time(link->expires_at),
            link_query_detail::pg_time(link->disabled_at),
            link->max_downloads,
            link->max_upload_files,
            link->max_upload_size_bytes,
            link->max_upload_total_bytes,
            vh::share::to_string(link->duplicate_policy),
            link_query_detail::pg_array(link->allowed_mime_types),
            link_query_detail::pg_array(link->blocked_mime_types),
            link_query_detail::pg_array(link->allowed_extensions),
            link_query_detail::pg_array(link->blocked_extensions),
            link->metadata.empty() ? "{}" : link->metadata
        };
        const auto res = txn.exec(pqxx::prepped{"share_link_update"}, p);
        link_query_detail::expect_row(res, "update");
        return std::make_shared<vh::share::Link>(res.one_row());
    });
}

void Link::revoke(const std::string& id, const uint32_t revoked_by) {
    link_query_detail::require_uuid(id, "id");
    Transactions::exec("share::Link::revoke", [&](pqxx::work& txn) {
        link_query_detail::expect_row(txn.exec(pqxx::prepped{"share_link_revoke"}, pqxx::params{id, revoked_by}), "revoke");
    });
}

void Link::rotateToken(const std::string& id, const std::string& lookup_id, const std::vector<uint8_t>& token_hash, const uint32_t updated_by) {
    link_query_detail::require_uuid(id, "id");
    link_query_detail::require_uuid(lookup_id, "lookup id");
    if (token_hash.empty()) throw std::invalid_argument("Share token hash is required");
    Transactions::exec("share::Link::rotateToken", [&](pqxx::work& txn) {
        link_query_detail::expect_row(txn.exec(pqxx::prepped{"share_link_rotate_token"}, pqxx::params{id, lookup_id, to_hex_bytea(token_hash), updated_by}), "rotateToken");
    });
}

void Link::touchAccess(const std::string& id) {
    link_query_detail::require_uuid(id, "id");
    Transactions::exec("share::Link::touchAccess", [&](pqxx::work& txn) {
        link_query_detail::expect_row(txn.exec(pqxx::prepped{"share_link_touch_access"}, id), "touchAccess");
    });
}

void Link::incrementDownload(const std::string& id) {
    link_query_detail::require_uuid(id, "id");
    Transactions::exec("share::Link::incrementDownload", [&](pqxx::work& txn) {
        link_query_detail::expect_row(txn.exec(pqxx::prepped{"share_link_increment_download"}, id), "incrementDownload");
    });
}

void Link::incrementUpload(const std::string& id) {
    link_query_detail::require_uuid(id, "id");
    Transactions::exec("share::Link::incrementUpload", [&](pqxx::work& txn) {
        link_query_detail::expect_row(txn.exec(pqxx::prepped{"share_link_increment_upload"}, id), "incrementUpload");
    });
}

}
