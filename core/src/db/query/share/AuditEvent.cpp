#include "db/query/share/AuditEvent.hpp"

#include "db/Transactions.hpp"
#include "db/model/ListQueryParams.hpp"
#include "share/AuditEvent.hpp"
#include "share/Types.hpp"

#include <pqxx/pqxx>

#include <regex>
#include <stdexcept>

namespace vh::db::query::share {
namespace audit_event_query_detail {
bool is_uuid(const std::string& value) {
    static const std::regex pattern("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(value, pattern);
}

void require_uuid(const std::string& value, const char* name) {
    if (!is_uuid(value)) throw std::invalid_argument(std::string("Invalid share audit ") + name);
}

std::pair<uint64_t, uint64_t> page(const db::model::ListQueryParams& params) {
    const uint64_t limit = params.limit.value_or(100);
    const uint64_t page = std::max<uint64_t>(params.page.value_or(1), 1);
    return {limit, (page - 1) * limit};
}

std::vector<std::shared_ptr<vh::share::AuditEvent>> events_from_result(const pqxx::result& res) {
    std::vector<std::shared_ptr<vh::share::AuditEvent>> out;
    out.reserve(res.size());
    for (const auto& row : res) out.push_back(std::make_shared<vh::share::AuditEvent>(row));
    return out;
}
}

void AuditEvent::append(const std::shared_ptr<vh::share::AuditEvent>& event) {
    if (!event) throw std::invalid_argument("Share audit event is required");
    if (event->share_id) audit_event_query_detail::require_uuid(*event->share_id, "share id");
    if (event->share_session_id) audit_event_query_detail::require_uuid(*event->share_session_id, "session id");
    if (event->event_type.empty()) throw std::invalid_argument("Share audit event type is required");

    Transactions::exec("share::AuditEvent::append", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"share_access_event_insert"}, pqxx::params{
            event->share_id,
            event->share_session_id,
            vh::share::to_string(event->actor_type),
            event->actor_user_id,
            event->event_type,
            event->vault_id,
            event->target_entry_id,
            event->target_path,
            vh::share::to_string(event->status),
            event->bytes_transferred,
            event->error_code,
            event->error_message,
            event->ip_address,
            event->user_agent
        });
    });
}

std::vector<std::shared_ptr<vh::share::AuditEvent>> AuditEvent::listForShare(const std::string& share_id, const db::model::ListQueryParams& params) {
    audit_event_query_detail::require_uuid(share_id, "share id");
    auto [limit, offset] = audit_event_query_detail::page(params);
    return Transactions::exec("share::AuditEvent::listForShare", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(share_id);
        p.append(static_cast<long long>(limit));
        p.append(static_cast<long long>(offset));
        return audit_event_query_detail::events_from_result(txn.exec(pqxx::prepped{"share_access_event_list_for_share"}, p));
    });
}

std::vector<std::shared_ptr<vh::share::AuditEvent>> AuditEvent::listForVault(const uint32_t vault_id, const db::model::ListQueryParams& params) {
    auto [limit, offset] = audit_event_query_detail::page(params);
    return Transactions::exec("share::AuditEvent::listForVault", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(vault_id);
        p.append(static_cast<long long>(limit));
        p.append(static_cast<long long>(offset));
        return audit_event_query_detail::events_from_result(txn.exec(pqxx::prepped{"share_access_event_list_for_vault"}, p));
    });
}

}
