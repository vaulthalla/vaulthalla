#include "db/query/share/EmailChallenge.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"
#include "share/EmailChallenge.hpp"

#include <pqxx/pqxx>

#include <regex>
#include <stdexcept>

namespace vh::db::query::share {

using vh::db::encoding::to_hex_bytea;

namespace email_challenge_query_detail {
bool is_uuid(const std::string& value) {
    static const std::regex pattern("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(value, pattern);
}

void require_uuid(const std::string& value, const char* name) {
    if (!is_uuid(value)) throw std::invalid_argument(std::string("Invalid share email challenge ") + name);
}

void expect_row(const pqxx::result& res, const char* operation) {
    if (res.empty()) throw std::runtime_error(std::string("Share email challenge mutation affected no rows: ") + operation);
}

std::string pg_time(const std::time_t value) { return vh::db::encoding::timestampToString(value); }
}

std::shared_ptr<vh::share::EmailChallenge> EmailChallenge::create(const std::shared_ptr<vh::share::EmailChallenge>& challenge) {
    if (!challenge) throw std::invalid_argument("Share email challenge is required");
    email_challenge_query_detail::require_uuid(challenge->share_id, "share id");
    if (challenge->share_session_id) email_challenge_query_detail::require_uuid(*challenge->share_session_id, "session id");
    if (challenge->email_hash.empty() || challenge->code_hash.empty()) throw std::invalid_argument("Share email challenge hashes are required");

    return Transactions::exec("share::EmailChallenge::create", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::EmailChallenge> {
        pqxx::params p{
            challenge->share_id,
            challenge->share_session_id,
            to_hex_bytea(challenge->email_hash),
            to_hex_bytea(challenge->code_hash),
            challenge->attempts,
            challenge->max_attempts,
            email_challenge_query_detail::pg_time(challenge->expires_at),
            challenge->ip_address,
            challenge->user_agent
        };
        const auto res = txn.exec(pqxx::prepped{"share_email_challenge_insert"}, p);
        return std::make_shared<vh::share::EmailChallenge>(res.one_row());
    });
}

std::shared_ptr<vh::share::EmailChallenge> EmailChallenge::get(const std::string& id) {
    email_challenge_query_detail::require_uuid(id, "id");
    return Transactions::exec("share::EmailChallenge::get", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::EmailChallenge> {
        const auto res = txn.exec(pqxx::prepped{"share_email_challenge_get"}, id);
        if (res.empty()) return nullptr;
        return std::make_shared<vh::share::EmailChallenge>(res.one_row());
    });
}

std::shared_ptr<vh::share::EmailChallenge> EmailChallenge::getActive(const std::string& share_id, const std::vector<uint8_t>& email_hash) {
    email_challenge_query_detail::require_uuid(share_id, "share id");
    if (email_hash.empty()) throw std::invalid_argument("Share email hash is required");
    return Transactions::exec("share::EmailChallenge::getActive", [&](pqxx::work& txn) -> std::shared_ptr<vh::share::EmailChallenge> {
        const auto res = txn.exec(pqxx::prepped{"share_email_challenge_get_active"}, pqxx::params{share_id, to_hex_bytea(email_hash)});
        if (res.empty()) return nullptr;
        return std::make_shared<vh::share::EmailChallenge>(res.one_row());
    });
}

void EmailChallenge::recordAttempt(const std::string& challenge_id) {
    email_challenge_query_detail::require_uuid(challenge_id, "id");
    Transactions::exec("share::EmailChallenge::recordAttempt", [&](pqxx::work& txn) {
        email_challenge_query_detail::expect_row(txn.exec(pqxx::prepped{"share_email_challenge_record_attempt"}, challenge_id), "recordAttempt");
    });
}

void EmailChallenge::consume(const std::string& challenge_id) {
    email_challenge_query_detail::require_uuid(challenge_id, "id");
    Transactions::exec("share::EmailChallenge::consume", [&](pqxx::work& txn) {
        email_challenge_query_detail::expect_row(txn.exec(pqxx::prepped{"share_email_challenge_consume"}, challenge_id), "consume");
    });
}

uint64_t EmailChallenge::purgeExpired() {
    return Transactions::exec("share::EmailChallenge::purgeExpired", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"share_email_challenge_purge_expired"}).size();
    });
}

}
