#include "db/query/share/Upload.hpp"

#include "db/Transactions.hpp"
#include "db/model/ListQueryParams.hpp"
#include "share/Types.hpp"
#include "share/Upload.hpp"

#include <pqxx/pqxx>

#include <regex>
#include <stdexcept>

namespace vh::db::query::share {
namespace upload_query_detail {
bool is_uuid(const std::string& value) {
    static const std::regex pattern("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(value, pattern);
}

void require_uuid(const std::string& value, const char* name) {
    if (!is_uuid(value)) throw std::invalid_argument(std::string("Invalid share upload ") + name);
}

std::pair<uint64_t, uint64_t> page(const db::model::ListQueryParams& params) {
    const uint64_t limit = params.limit.value_or(100);
    const uint64_t page = std::max<uint64_t>(params.page.value_or(1), 1);
    return {limit, (page - 1) * limit};
}

void expect_row(const pqxx::result& res, const char* operation) {
    if (res.empty()) throw std::runtime_error(std::string("Share upload mutation affected no rows: ") + operation);
}

std::vector<std::shared_ptr<vh::share::Upload>> uploads_from_result(const pqxx::result& res) {
    std::vector<std::shared_ptr<vh::share::Upload>> out;
    out.reserve(res.size());
    for (const auto& row : res) out.push_back(std::make_shared<vh::share::Upload>(row));
    return out;
}
}

std::shared_ptr<vh::share::Upload> Upload::create(const std::shared_ptr<vh::share::Upload>& upload) {
    if (!upload) throw std::invalid_argument("Share upload is required");
    upload_query_detail::require_uuid(upload->share_id, "share id");
    upload_query_detail::require_uuid(upload->share_session_id, "session id");
    if (upload->target_parent_entry_id == 0) throw std::invalid_argument("Share upload target parent is required");
    if (upload->target_path.empty() || upload->target_path.front() != '/') throw std::invalid_argument("Share upload target path must be absolute");

    return Transactions::exec("share::Upload::create", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Upload> {
        pqxx::params p{
            upload->share_id,
            upload->share_session_id,
            upload->target_parent_entry_id,
            upload->target_path,
            upload->tmp_path,
            upload->original_filename,
            upload->resolved_filename,
            upload->expected_size_bytes,
            upload->received_size_bytes,
            upload->mime_type,
            upload->content_hash,
            upload->created_entry_id,
            vh::share::to_string(upload->status),
            upload->error
        };
        const auto res = txn.exec(pqxx::prepped{"share_upload_insert"}, p);
        return std::make_shared<vh::share::Upload>(res.one_row());
    });
}

std::shared_ptr<vh::share::Upload> Upload::get(const std::string& upload_id) {
    upload_query_detail::require_uuid(upload_id, "id");
    return Transactions::exec("share::Upload::get", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Upload> {
        const auto res = txn.exec(pqxx::prepped{"share_upload_get"}, upload_id);
        if (res.empty()) return nullptr;
        return std::make_shared<vh::share::Upload>(res.one_row());
    });
}

void Upload::addReceivedBytes(const std::string& upload_id, const uint64_t bytes) {
    upload_query_detail::require_uuid(upload_id, "id");
    Transactions::exec("share::Upload::addReceivedBytes", [&](pqxx::work& txn) {
        upload_query_detail::expect_row(txn.exec(pqxx::prepped{"share_upload_add_received_bytes"}, pqxx::params{upload_id, static_cast<long long>(bytes)}), "addReceivedBytes");
    });
}

void Upload::complete(const std::string& upload_id, const uint32_t entry_id, const std::string& content_hash, const std::string& mime_type) {
    upload_query_detail::require_uuid(upload_id, "id");
    Transactions::exec("share::Upload::complete", [&](pqxx::work& txn) {
        upload_query_detail::expect_row(txn.exec(pqxx::prepped{"share_upload_complete"}, pqxx::params{upload_id, entry_id, content_hash, mime_type}), "complete");
    });
}

void Upload::fail(const std::string& upload_id, const std::string& error) {
    upload_query_detail::require_uuid(upload_id, "id");
    Transactions::exec("share::Upload::fail", [&](pqxx::work& txn) {
        upload_query_detail::expect_row(txn.exec(pqxx::prepped{"share_upload_fail"}, pqxx::params{upload_id, error}), "fail");
    });
}

void Upload::cancel(const std::string& upload_id) {
    upload_query_detail::require_uuid(upload_id, "id");
    Transactions::exec("share::Upload::cancel", [&](pqxx::work& txn) {
        upload_query_detail::expect_row(txn.exec(pqxx::prepped{"share_upload_cancel"}, upload_id), "cancel");
    });
}

uint64_t Upload::sumCompletedBytes(const std::string& share_id) {
    upload_query_detail::require_uuid(share_id, "share id");
    return Transactions::exec("share::Upload::sumCompletedBytes", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"share_upload_sum_completed_bytes"}, share_id).one_field().as<uint64_t>();
    });
}

uint64_t Upload::countCompletedFiles(const std::string& share_id) {
    upload_query_detail::require_uuid(share_id, "share id");
    return Transactions::exec("share::Upload::countCompletedFiles", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"share_upload_count_completed_files"}, share_id).one_field().as<uint64_t>();
    });
}

std::vector<std::shared_ptr<vh::share::Upload>> Upload::listForShare(const std::string& share_id, const db::model::ListQueryParams& params) {
    upload_query_detail::require_uuid(share_id, "share id");
    auto [limit, offset] = upload_query_detail::page(params);
    return Transactions::exec("share::Upload::listForShare", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(share_id);
        p.append(static_cast<long long>(limit));
        p.append(static_cast<long long>(offset));
        return upload_query_detail::uploads_from_result(txn.exec(pqxx::prepped{"share_upload_list_for_share"}, p));
    });
}

}
