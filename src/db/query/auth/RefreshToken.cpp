#include "db/query/auth/RefreshToken.hpp"
#include "auth/model/RefreshToken.hpp"
#include "identities/model/User.hpp"
#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "log/Registry.hpp"

#include <chrono>

using namespace vh::db::query::auth;

void RefreshToken::set(const std::shared_ptr<vh::auth::model::RefreshToken>& token) {
    if (!token) {
        log::Registry::db()->error("[RefreshToken] Attempted to set a null refresh token");
        return;
    }

    Transactions::exec("RefreshToken::setRefreshToken", [&](pqxx::work& txn) {
        const pqxx::params p{
            token->jti,
            token->userId,
            token->hashedToken,
            token->ipAddress,
            token->userAgent,
            encoding::timestampToString(std::chrono::system_clock::to_time_t(token->issuedAt)),
            encoding::timestampToString(std::chrono::system_clock::to_time_t(token->expiresAt)),
        };

        txn.exec(pqxx::prepped{"insert_refresh_token"}, p);
    });
}

void RefreshToken::remove(const std::string& jti) {
    Transactions::exec("RefreshToken::removeRefreshToken", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"delete_refresh_token_by_jti"}, pqxx::params{jti});
    });
}

std::shared_ptr<vh::auth::model::RefreshToken> RefreshToken::get(const std::string& jti) {
    return Transactions::exec(
        "RefreshToken::getRefreshToken",
        [&](pqxx::work& txn) -> std::shared_ptr<vh::auth::model::RefreshToken> {
            const auto res = txn.exec(pqxx::prepped{"get_refresh_token_by_jti"}, pqxx::params{jti});

            if (res.empty()) {
                log::Registry::db()->trace("[RefreshToken] No refresh token found for JTI: {}", jti);
                return nullptr;
            }

            return std::make_shared<vh::auth::model::RefreshToken>(res.one_row());
        });
}

std::vector<std::shared_ptr<vh::auth::model::RefreshToken>> RefreshToken::list(const unsigned int userId) {
    return Transactions::exec("RefreshToken::listRefreshTokens", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"list_active_refresh_tokens_for_user"}, pqxx::params{userId});

        std::vector<std::shared_ptr<vh::auth::model::RefreshToken>> tokens;
        tokens.reserve(res.size());

        for (const auto& row : res)
            tokens.push_back(std::make_shared<vh::auth::model::RefreshToken>(row));

        return tokens;
    });
}

void RefreshToken::touch(const std::string& jti) {
    Transactions::exec("RefreshToken::touchRefreshToken", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"touch_refresh_token_last_used"}, pqxx::params{jti});
    });
}

void RefreshToken::refresh(const std::string& jti) {
    Transactions::exec("RefreshToken::revokeRefreshToken", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"revoke_refresh_token_by_jti"}, pqxx::params{jti});
    });
}

void RefreshToken::revokeAll(const unsigned int userId) {
    Transactions::exec("RefreshToken::revokeAllRefreshTokens", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"revoke_all_refresh_tokens_for_user"}, pqxx::params{userId});
    });
}

void RefreshToken::purgeExpired(const unsigned int userId) {
    Transactions::exec("RefreshToken::purgeExpiredRefreshTokens", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"delete_expired_refresh_tokens_for_user"}, pqxx::params{userId});
    });
}

void RefreshToken::purgeOldRevoked() {
    Transactions::exec("RefreshToken::purgeOldRevokedRefreshTokens", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"delete_old_revoked_refresh_tokens_global"});
    });
}

void RefreshToken::revokeAndPurge(const unsigned int userId) {
    Transactions::exec("RefreshToken::revokeAndPurgeRefreshTokens", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"revoke_all_refresh_tokens_for_user"}, pqxx::params{userId});
        txn.exec(pqxx::prepped{"delete_expired_refresh_tokens_for_user"}, pqxx::params{userId});
    });
}

std::shared_ptr<vh::identities::model::User> RefreshToken::getUserByJti(const std::string& jti) {
    return Transactions::exec(
        "RefreshToken::getUserByRefreshToken",
        [&](pqxx::work& txn) -> std::shared_ptr<identities::model::User> {
            const auto res = txn.exec(pqxx::prepped{"get_user_by_refresh_token_jti"}, pqxx::params{jti});

            if (res.empty()) {
                log::Registry::db()->trace("[RefreshToken] No user found for refresh token JTI: {}", jti);
                return nullptr;
            }

            const auto userRow = res.one_row();
            const auto userId = userRow["id"].as<unsigned int>();

            const auto userRoleRow =
                txn.exec(pqxx::prepped{"get_user_assigned_role"}, pqxx::params{userId}).one_row();

            const auto rolesRes =
                txn.exec(pqxx::prepped{"get_user_and_group_assigned_vault_roles"}, pqxx::params{userId});

            const auto overridesRes =
                txn.exec(pqxx::prepped{"list_user_and_group_permission_overrides"}, pqxx::params{userId});

            return std::make_shared<identities::model::User>(
                userRow,
                userRoleRow,
                rolesRes,
                overridesRes
            );
        });
}
