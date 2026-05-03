#include "db/query/share/Session.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"
#include "share/Session.hpp"

#include <pqxx/pqxx>

#include <regex>
#include <stdexcept>

namespace vh::db::query::share {

using vh::db::encoding::to_hex_bytea;

namespace session_query_detail {
bool is_uuid(const std::string& value) {
    static const std::regex pattern("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(value, pattern);
}

void require_uuid(const std::string& value, const char* name) {
    if (!is_uuid(value)) throw std::invalid_argument(std::string("Invalid share session ") + name);
}

void expect_row(const pqxx::result& res, const char* operation) {
    if (res.empty()) throw std::runtime_error(std::string("Share session mutation affected no rows: ") + operation);
}

std::optional<std::string> pg_time(const std::optional<std::time_t>& value) {
    if (!value) return std::nullopt;
    return vh::db::encoding::timestampToString(*value);
}

std::string pg_time(const std::time_t value) { return vh::db::encoding::timestampToString(value); }
}

std::shared_ptr<vh::share::Session> Session::create(const std::shared_ptr<vh::share::Session>& session) {
    if (!session) throw std::invalid_argument("Share session is required");
    session_query_detail::require_uuid(session->share_id, "share id");
    session_query_detail::require_uuid(session->session_token_lookup_id, "lookup id");
    if (session->session_token_hash.empty()) throw std::invalid_argument("Share session token hash is required");

    return Transactions::exec("share::Session::create", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Session> {
        const std::optional<std::string> email_hash = session->email_hash ? std::make_optional(to_hex_bytea(*session->email_hash)) : std::nullopt;
        pqxx::params p{
            session->share_id,
            session->session_token_lookup_id,
            to_hex_bytea(session->session_token_hash),
            email_hash,
            session_query_detail::pg_time(session->verified_at),
            session_query_detail::pg_time(session->expires_at),
            session->ip_address,
            session->user_agent
        };
        const auto res = txn.exec(pqxx::prepped{"share_session_insert"}, p);
        return std::make_shared<vh::share::Session>(res.one_row());
    });
}

std::shared_ptr<vh::share::Session> Session::get(const std::string& id) {
    session_query_detail::require_uuid(id, "id");
    return Transactions::exec("share::Session::get", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Session> {
        const auto res = txn.exec(pqxx::prepped{"share_session_get"}, id);
        if (res.empty()) return nullptr;
        return std::make_shared<vh::share::Session>(res.one_row());
    });
}

std::shared_ptr<vh::share::Session> Session::getByLookupId(const std::string& lookup_id) {
    session_query_detail::require_uuid(lookup_id, "lookup id");
    return Transactions::exec("share::Session::getByLookupId", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::Session> {
        const auto res = txn.exec(pqxx::prepped{"share_session_get_by_lookup_id"}, lookup_id);
        if (res.empty()) return nullptr;
        return std::make_shared<vh::share::Session>(res.one_row());
    });
}

void Session::verify(const std::string& session_id, const std::vector<uint8_t>& email_hash) {
    session_query_detail::require_uuid(session_id, "id");
    if (email_hash.empty()) throw std::invalid_argument("Share session email hash is required");
    Transactions::exec("share::Session::verify", [&](pqxx::work& txn) {
        session_query_detail::expect_row(txn.exec(pqxx::prepped{"share_session_verify"}, pqxx::params{session_id, to_hex_bytea(email_hash)}), "verify");
    });
}

void Session::touch(const std::string& session_id) {
    session_query_detail::require_uuid(session_id, "id");
    Transactions::exec("share::Session::touch", [&](pqxx::work& txn) {
        session_query_detail::expect_row(txn.exec(pqxx::prepped{"share_session_touch"}, session_id), "touch");
    });
}

void Session::revoke(const std::string& session_id) {
    session_query_detail::require_uuid(session_id, "id");
    Transactions::exec("share::Session::revoke", [&](pqxx::work& txn) {
        session_query_detail::expect_row(txn.exec(pqxx::prepped{"share_session_revoke"}, session_id), "revoke");
    });
}

void Session::revokeForShare(const std::string& share_id) {
    session_query_detail::require_uuid(share_id, "share id");
    Transactions::exec("share::Session::revokeForShare", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"share_session_revoke_for_share"}, share_id);
    });
}

uint64_t Session::purgeExpired() {
    return Transactions::exec("share::Session::purgeExpired", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"share_session_purge_expired"}).size();
    });
}

}
